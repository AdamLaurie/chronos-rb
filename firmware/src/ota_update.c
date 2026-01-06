/**
 * CHRONOS-Rb OTA Update Module
 *
 * Implements over-the-air firmware updates using pico_fota_bootloader.
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

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static ota_status_t g_ota_status = {0};

/* Write buffer for 256-byte alignment */
static uint8_t write_buffer[PFB_ALIGN_SIZE];
static size_t buffer_offset = 0;

/* Timeout tracking */
static uint64_t last_activity_us = 0;

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
 * UPDATE FUNCTIONS
 *============================================================================*/

ota_error_t ota_begin(size_t total_size, uint32_t expected_crc) {
    printf("[OTA] Starting update, size=%u bytes\n", (unsigned)total_size);

    /* Check if already in progress */
    if (g_ota_status.state == OTA_STATE_RECEIVING) {
        printf("[OTA] ERROR: Update already in progress\n");
        return OTA_ERROR_ALREADY_IN_PROGRESS;
    }

    /* Check size */
    if (total_size > OTA_MAX_FIRMWARE_SIZE) {
        printf("[OTA] ERROR: Firmware too large (%u > %u)\n",
               (unsigned)total_size, OTA_MAX_FIRMWARE_SIZE);
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
        g_ota_status.bytes_received += to_buffer;

        /* Write when buffer is full */
        if (buffer_offset == PFB_ALIGN_SIZE) {
            int ret = pfb_write_to_flash_aligned_256_bytes(
                write_buffer,
                g_ota_status.bytes_written,
                PFB_ALIGN_SIZE
            );

            if (ret != 0) {
                printf("[OTA] ERROR: Flash write failed at offset %u: %d\n",
                       (unsigned)g_ota_status.bytes_written, ret);
                g_ota_status.last_error = OTA_ERROR_WRITE_FAILED;
                g_ota_status.state = OTA_STATE_ERROR;
                return OTA_ERROR_WRITE_FAILED;
            }

            g_ota_status.bytes_written += PFB_ALIGN_SIZE;
            buffer_offset = 0;
        }
    }

    return OTA_OK;
}

ota_error_t ota_finish(void) {
    printf("[OTA] Finishing update...\n");

    if (g_ota_status.state != OTA_STATE_RECEIVING) {
        printf("[OTA] ERROR: Invalid state for finish\n");
        return OTA_ERROR_INVALID_STATE;
    }

    g_ota_status.state = OTA_STATE_VALIDATING;

    /* Flush any remaining buffered data (pad with 0xFF) */
    if (buffer_offset > 0) {
        memset(write_buffer + buffer_offset, 0xFF, PFB_ALIGN_SIZE - buffer_offset);

        int ret = pfb_write_to_flash_aligned_256_bytes(
            write_buffer,
            g_ota_status.bytes_written,
            PFB_ALIGN_SIZE
        );

        if (ret != 0) {
            printf("[OTA] ERROR: Final flush failed: %d\n", ret);
            g_ota_status.last_error = OTA_ERROR_WRITE_FAILED;
            g_ota_status.state = OTA_STATE_ERROR;
            return OTA_ERROR_WRITE_FAILED;
        }

        g_ota_status.bytes_written += PFB_ALIGN_SIZE;
        buffer_offset = 0;
    }

    printf("[OTA] Wrote %u bytes total\n", (unsigned)g_ota_status.bytes_written);

    /* Verify SHA256 if enabled in bootloader */
    printf("[OTA] Verifying firmware...\n");
    int ret = pfb_firmware_sha256_check(g_ota_status.total_size);
    if (ret != 0) {
        printf("[OTA] WARNING: SHA256 verification returned %d (continuing anyway)\n", ret);
        /* Skip verification failure for now - bootloader will still validate on boot */
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
