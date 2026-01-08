/**
 * CHRONOS-Rb OTA Update Module
 *
 * Implements over-the-air firmware updates using pico_fota_bootloader.
 *
 * With GZIP compression enabled, uses store-then-decompress approach:
 * 1. Store encrypted+gzip data to upper half of download slot
 * 2. On finish: decrypt, decompress, write to lower half
 * 3. Bootloader does normal swap with decompressed data
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico_fota_bootloader/core.h"

#include "ota_update.h"

#ifdef PFB_WITH_GZIP_COMPRESSION
#include "../deps/pico_fota_bootloader/src/uzlib/uzlib.h"
#endif

#ifdef PFB_WITH_IMAGE_ENCRYPTION
#include <mbedtls/aes.h>
#endif

/* Linker symbols for flash layout */
extern char __FLASH_DOWNLOAD_SLOT_START[];
extern char __FLASH_SWAP_SPACE_LENGTH[];

/*============================================================================
 * CONSTANTS
 *============================================================================*/

/* Offset where compressed data is stored (1MB into download slot) */
#define COMPRESSED_DATA_OFFSET  (1024 * 1024)

/* Size for decompression dictionary */
#define GZIP_DICT_SIZE  32768

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static ota_status_t g_ota_status = {0};

/* Write buffer for 256-byte alignment */
static uint8_t write_buffer[PFB_ALIGN_SIZE];
static size_t buffer_offset = 0;

/* Timeout tracking */
static uint64_t last_activity_us = 0;

#ifdef PFB_WITH_GZIP_COMPRESSION
/* Decompression state - only used during ota_finish */
static struct uzlib_uncomp g_decomp;
static uint8_t g_dict[GZIP_DICT_SIZE];
#endif

#ifdef PFB_WITH_IMAGE_ENCRYPTION
static mbedtls_aes_context g_aes_ctx;
#endif

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

void ota_init(void) {
    printf("[OTA] Initializing OTA subsystem\n");

    memset(&g_ota_status, 0, sizeof(g_ota_status));
    g_ota_status.state = OTA_STATE_IDLE;
    buffer_offset = 0;

    /* Check if we just completed a firmware update */
    if (pfb_is_after_firmware_update()) {
        g_ota_status.is_after_update = true;
        printf("[OTA] System booted after firmware update\n");
    }

    /* Check if bootloader performed a rollback */
    if (pfb_is_after_rollback()) {
        g_ota_status.is_after_rollback = true;
        printf("[OTA] WARNING: Bootloader performed rollback to previous firmware\n");
    }

    printf("[OTA] Ready for updates\n");
}

/*============================================================================
 * STATUS FUNCTIONS
 *============================================================================*/

const ota_status_t* ota_get_status(void) {
    return &g_ota_status;
}

bool ota_is_ready(void) {
    return g_ota_status.state == OTA_STATE_IDLE ||
           g_ota_status.state == OTA_STATE_READY ||
           g_ota_status.state == OTA_STATE_ERROR;
}

uint8_t ota_get_progress(void) {
    if (g_ota_status.total_size == 0) {
        return 0;
    }
    return (uint8_t)((g_ota_status.bytes_received * 100) / g_ota_status.total_size);
}

const char* ota_error_str(ota_error_t error) {
    switch (error) {
        case OTA_OK:                    return "OK";
        case OTA_ERROR_NOT_INITIALIZED: return "Not initialized";
        case OTA_ERROR_ALREADY_IN_PROGRESS: return "Update already in progress";
        case OTA_ERROR_SIZE_TOO_LARGE:  return "Firmware too large";
        case OTA_ERROR_ALIGNMENT:       return "Alignment error";
        case OTA_ERROR_WRITE_FAILED:    return "Flash write failed";
        case OTA_ERROR_VERIFY_FAILED:   return "Verification failed";
        case OTA_ERROR_INVALID_STATE:   return "Invalid state";
        case OTA_ERROR_FLASH_INIT:      return "Flash init failed";
        case OTA_ERROR_DECOMPRESS:      return "Decompression failed";
        default:                        return "Unknown error";
    }
}

const char* ota_state_str(ota_state_t state) {
    switch (state) {
        case OTA_STATE_IDLE:       return "Idle";
        case OTA_STATE_RECEIVING:  return "Receiving";
        case OTA_STATE_VALIDATING: return "Validating";
        case OTA_STATE_READY:      return "Ready";
        case OTA_STATE_ERROR:      return "Error";
        default:                   return "Unknown";
    }
}

/*============================================================================
 * FLASH WRITE HELPER
 *============================================================================*/

/* Write raw data directly to flash at given offset in download slot */
static int write_raw_to_flash(const uint8_t *data, size_t offset, size_t len) {
    uint32_t dest = (uint32_t)__FLASH_DOWNLOAD_SLOT_START - XIP_BASE + offset;

    uint32_t saved_interrupts = save_and_disable_interrupts();
    flash_range_program(dest, data, len);
    restore_interrupts(saved_interrupts);

    return 0;
}

/*============================================================================
 * UPDATE FUNCTIONS
 *============================================================================*/

ota_error_t ota_begin(size_t total_size, uint32_t expected_crc) {
    printf("[OTA] Starting update, size=%u bytes\n", (unsigned)total_size);

    /* Check if already in progress */
    if (g_ota_status.state == OTA_STATE_RECEIVING) {
        printf("[OTA] ERROR: Update already in progress\n");
        return OTA_ERROR_ALREADY_IN_PROGRESS;
    }

    /* Check size - compressed data must fit in upper half of slot */
    size_t max_compressed = (size_t)__FLASH_SWAP_SPACE_LENGTH - COMPRESSED_DATA_OFFSET;
    if (total_size > max_compressed) {
        printf("[OTA] ERROR: Firmware too large (%u > %u)\n",
               (unsigned)total_size, (unsigned)max_compressed);
        g_ota_status.last_error = OTA_ERROR_SIZE_TOO_LARGE;
        g_ota_status.state = OTA_STATE_ERROR;
        return OTA_ERROR_SIZE_TOO_LARGE;
    }

    /* Initialize download slot (erases flash) */
    printf("[OTA] Initializing download slot...\n");
    int ret = pfb_initialize_download_slot();
    if (ret != 0) {
        printf("[OTA] ERROR: Failed to initialize download slot: %d\n", ret);
        g_ota_status.last_error = OTA_ERROR_FLASH_INIT;
        g_ota_status.state = OTA_STATE_ERROR;
        return OTA_ERROR_FLASH_INIT;
    }

    /* Reset state */
    g_ota_status.state = OTA_STATE_RECEIVING;
    g_ota_status.total_size = total_size;
    g_ota_status.bytes_received = 0;
    g_ota_status.bytes_written = 0;
    g_ota_status.expected_crc = expected_crc;
    g_ota_status.last_error = OTA_OK;
    buffer_offset = 0;
    last_activity_us = time_us_64();

    printf("[OTA] Ready to receive firmware\n");
    return OTA_OK;
}

ota_error_t ota_write_chunk(const uint8_t *data, size_t len) {
    if (g_ota_status.state != OTA_STATE_RECEIVING) {
        return OTA_ERROR_INVALID_STATE;
    }

    /* Update activity timestamp */
    last_activity_us = time_us_64();

    /*
     * Store raw encrypted+gzip data to upper area of download slot.
     * No decryption or decompression here - that happens in ota_finish.
     */
    size_t data_offset = 0;

    while (data_offset < len) {
        /* Calculate how much we can buffer */
        size_t to_buffer = len - data_offset;
        size_t space = PFB_ALIGN_SIZE - buffer_offset;
        if (to_buffer > space) {
            to_buffer = space;
        }

        /* Copy to buffer */
        memcpy(write_buffer + buffer_offset, data + data_offset, to_buffer);
        buffer_offset += to_buffer;
        data_offset += to_buffer;

        /* Write when buffer is full */
        if (buffer_offset == PFB_ALIGN_SIZE) {
            /* Write to upper area (COMPRESSED_DATA_OFFSET + bytes_received) */
            size_t flash_offset = COMPRESSED_DATA_OFFSET + g_ota_status.bytes_received;
            int ret = write_raw_to_flash(write_buffer, flash_offset, PFB_ALIGN_SIZE);

            if (ret != 0) {
                printf("[OTA] ERROR: Flash write failed at offset %u: %d\n",
                       (unsigned)flash_offset, ret);
                g_ota_status.last_error = OTA_ERROR_WRITE_FAILED;
                g_ota_status.state = OTA_STATE_ERROR;
                return OTA_ERROR_WRITE_FAILED;
            }

            g_ota_status.bytes_received += PFB_ALIGN_SIZE;
            buffer_offset = 0;
        }
    }

    return OTA_OK;
}

#ifdef PFB_WITH_GZIP_COMPRESSION
/*
 * Decompress stored firmware from upper to lower area of download slot.
 * Uses uzlib callback mechanism for proper streaming.
 */
#define DECOMP_CHUNK_SIZE  4096  /* Read/decrypt chunk size */

/* State for the read callback */
static struct {
    const uint8_t *flash_base;
    size_t flash_offset;
    size_t flash_size;
    uint8_t buffer[DECOMP_CHUNK_SIZE];
} g_read_state;

/* Callback: uzlib calls this when it needs more input bytes */
static int decomp_read_cb(struct uzlib_uncomp *uncomp) {
    /* Check if we've read everything */
    if (g_read_state.flash_offset >= g_read_state.flash_size) {
        return -1;  /* EOF */
    }

    /* Read and decrypt a chunk */
    size_t remaining = g_read_state.flash_size - g_read_state.flash_offset;
    size_t chunk = (remaining < DECOMP_CHUNK_SIZE) ? remaining : DECOMP_CHUNK_SIZE;
    /* Align to 16 bytes for AES */
    chunk = (chunk / 16) * 16;
    if (chunk == 0) chunk = remaining;  /* Last few bytes */

#ifdef PFB_WITH_IMAGE_ENCRYPTION
    for (size_t i = 0; i + 16 <= chunk; i += 16) {
        mbedtls_aes_crypt_ecb(&g_aes_ctx, MBEDTLS_AES_DECRYPT,
                              g_read_state.flash_base + g_read_state.flash_offset + i,
                              g_read_state.buffer + i);
    }
#else
    memcpy(g_read_state.buffer, g_read_state.flash_base + g_read_state.flash_offset, chunk);
#endif

    g_read_state.flash_offset += chunk;

    /* Update uzlib source pointers to the new buffer */
    uncomp->source = g_read_state.buffer;
    uncomp->source_limit = g_read_state.buffer + chunk;

    /* Return first byte */
    return *uncomp->source++;
}

static ota_error_t decompress_firmware(size_t compressed_size) {
    printf("[OTA] Decompressing %u bytes...\n", (unsigned)compressed_size);

    /* Initialize read state */
    g_read_state.flash_base = (const uint8_t *)__FLASH_DOWNLOAD_SLOT_START + COMPRESSED_DATA_OFFSET;
    g_read_state.flash_offset = 0;
    g_read_state.flash_size = compressed_size;

#ifdef PFB_WITH_IMAGE_ENCRYPTION
    const char *aes_key = PFB_AES_KEY;
    mbedtls_aes_init(&g_aes_ctx);
    if (mbedtls_aes_setkey_dec(&g_aes_ctx, (const unsigned char *)aes_key,
                                strlen(aes_key) * 8) != 0) {
        printf("[OTA] ERROR: AES init failed\n");
        return OTA_ERROR_DECOMPRESS;
    }
#endif

    /* Initialize decompressor with callback */
    memset(&g_decomp, 0, sizeof(g_decomp));
    uzlib_uncompress_init(&g_decomp, g_dict, GZIP_DICT_SIZE);
    g_decomp.source = NULL;
    g_decomp.source_limit = NULL;
    g_decomp.source_read_cb = decomp_read_cb;

    /* Parse gzip header */
    int res = uzlib_gzip_parse_header(&g_decomp);
    if (res != TINF_OK) {
        printf("[OTA] ERROR: Header parse failed: %d\n", res);
        goto fail;
    }
    printf("[OTA] Gzip header OK\n");

    /* Decompress to flash */
    static uint8_t decomp_out[PFB_ALIGN_SIZE];
    size_t write_offset = 0;

    while (1) {
        g_decomp.dest_start = decomp_out;
        g_decomp.dest = decomp_out;
        g_decomp.dest_limit = decomp_out + PFB_ALIGN_SIZE;

        res = uzlib_uncompress_chksum(&g_decomp);

        size_t produced = g_decomp.dest - decomp_out;
        if (produced > 0) {
            if (produced < PFB_ALIGN_SIZE) {
                memset(decomp_out + produced, 0xFF, PFB_ALIGN_SIZE - produced);
            }
            write_raw_to_flash(decomp_out, write_offset, PFB_ALIGN_SIZE);
            write_offset += produced;

            if ((write_offset & 0x1FFFF) == 0) {
                printf("[OTA] %uKB...\n", (unsigned)(write_offset / 1024));
            }
        }

        if (res == TINF_DONE) {
            break;
        } else if (res != TINF_OK) {
            printf("[OTA] ERROR: Decompress failed: %d at %u\n", res, (unsigned)write_offset);
            goto fail;
        }
    }

#ifdef PFB_WITH_IMAGE_ENCRYPTION
    mbedtls_aes_free(&g_aes_ctx);
#endif
    g_ota_status.bytes_written = write_offset;
    printf("[OTA] Done: %u -> %u bytes\n", (unsigned)compressed_size, (unsigned)write_offset);
    return OTA_OK;

fail:
#ifdef PFB_WITH_IMAGE_ENCRYPTION
    mbedtls_aes_free(&g_aes_ctx);
#endif
    return OTA_ERROR_DECOMPRESS;
}
#endif /* PFB_WITH_GZIP_COMPRESSION */

ota_error_t ota_finish(void) {
    printf("[OTA] Finishing update...\n");

    if (g_ota_status.state != OTA_STATE_RECEIVING) {
        printf("[OTA] ERROR: Invalid state for finish\n");
        return OTA_ERROR_INVALID_STATE;
    }

    /* Flush any remaining buffered data */
    if (buffer_offset > 0) {
        /* Pad with 0xFF to align */
        memset(write_buffer + buffer_offset, 0xFF, PFB_ALIGN_SIZE - buffer_offset);

        size_t flash_offset = COMPRESSED_DATA_OFFSET + g_ota_status.bytes_received;
        int ret = write_raw_to_flash(write_buffer, flash_offset, PFB_ALIGN_SIZE);

        if (ret != 0) {
            printf("[OTA] ERROR: Final flush failed: %d\n", ret);
            g_ota_status.last_error = OTA_ERROR_WRITE_FAILED;
            g_ota_status.state = OTA_STATE_ERROR;
            return OTA_ERROR_WRITE_FAILED;
        }

        g_ota_status.bytes_received += buffer_offset;
        buffer_offset = 0;
    }

    printf("[OTA] Stored %u bytes of compressed data\n", (unsigned)g_ota_status.bytes_received);

    g_ota_status.state = OTA_STATE_VALIDATING;

#ifdef PFB_WITH_GZIP_COMPRESSION
    /* Decompress from upper to lower area */
    ota_error_t err = decompress_firmware(g_ota_status.bytes_received);
    if (err != OTA_OK) {
        g_ota_status.last_error = err;
        g_ota_status.state = OTA_STATE_ERROR;
        return err;
    }
#else
    /* Without compression, data was written directly - just track size */
    g_ota_status.bytes_written = g_ota_status.bytes_received;
    printf("[OTA] Wrote %u bytes total\n", (unsigned)g_ota_status.bytes_written);
#endif

    /* Verify SHA256 if enabled in bootloader */
    printf("[OTA] Verifying firmware...\n");
    int ret = pfb_firmware_sha256_check(g_ota_status.bytes_written);
    if (ret != 0) {
        printf("[OTA] WARNING: SHA256 verification returned %d (continuing anyway)\n", ret);
    }

    /* Mark download slot as valid */
    printf("[OTA] Marking firmware as valid\n");
    pfb_mark_download_slot_as_valid();

    g_ota_status.state = OTA_STATE_READY;
    printf("[OTA] Update ready! Call ota_apply_and_reboot() to apply.\n");

    return OTA_OK;
}

void ota_abort(void) {
    printf("[OTA] Aborting update\n");

    /* Mark download slot as invalid */
    pfb_mark_download_slot_as_invalid();

    /* Reset state */
    g_ota_status.state = OTA_STATE_IDLE;
    g_ota_status.total_size = 0;
    g_ota_status.bytes_received = 0;
    g_ota_status.bytes_written = 0;
    g_ota_status.expected_crc = 0;
    buffer_offset = 0;

    printf("[OTA] Update aborted\n");
}

void ota_apply_and_reboot(void) {
    if (g_ota_status.state != OTA_STATE_READY) {
        printf("[OTA] ERROR: Cannot apply - state is %s\n",
               ota_state_str(g_ota_status.state));
        return;
    }

    printf("[OTA] Applying update and rebooting...\n");
    sleep_ms(100);  /* Let message flush */

    /* This function does not return */
    pfb_perform_update();
}

void ota_confirm_boot(void) {
    printf("[OTA] Confirming boot success (preventing rollback)\n");
    pfb_firmware_commit();
}

void ota_task(void) {
    /* Check for timeout during receiving state */
    if (g_ota_status.state == OTA_STATE_RECEIVING && last_activity_us > 0) {
        uint64_t now = time_us_64();
        uint64_t elapsed_sec = (now - last_activity_us) / 1000000;

        if (elapsed_sec >= OTA_TIMEOUT_SEC) {
            printf("[OTA] Upload timeout after %llu seconds - aborting\n",
                   (unsigned long long)elapsed_sec);
            ota_abort();
        }
    }
}
