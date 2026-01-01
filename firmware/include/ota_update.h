/**
 * CHRONOS-Rb OTA Update Module
 *
 * Provides over-the-air firmware update capabilities using
 * pico_fota_bootloader for A/B partition updates with automatic rollback.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*============================================================================
 * OTA CONSTANTS
 *============================================================================*/

/* Chunk size for writing to flash (must be 256-byte aligned) */
#define OTA_CHUNK_SIZE          256

/* Maximum firmware size (slightly under 1MB to be safe) */
#define OTA_MAX_FIRMWARE_SIZE   (1024 * 1024)

/*============================================================================
 * OTA STATUS CODES
 *============================================================================*/

typedef enum {
    OTA_OK = 0,
    OTA_ERROR_NOT_INITIALIZED,
    OTA_ERROR_ALREADY_IN_PROGRESS,
    OTA_ERROR_SIZE_TOO_LARGE,
    OTA_ERROR_ALIGNMENT,
    OTA_ERROR_WRITE_FAILED,
    OTA_ERROR_VERIFY_FAILED,
    OTA_ERROR_INVALID_STATE,
    OTA_ERROR_FLASH_INIT
} ota_error_t;

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_VALIDATING,
    OTA_STATE_READY,
    OTA_STATE_ERROR
} ota_state_t;

/*============================================================================
 * OTA STATUS STRUCTURE
 *============================================================================*/

typedef struct {
    ota_state_t state;
    size_t total_size;          /* Expected total firmware size */
    size_t bytes_received;      /* Bytes received so far */
    size_t bytes_written;       /* Bytes written to flash */
    uint32_t expected_crc;      /* Expected CRC32 (optional) */
    ota_error_t last_error;
    bool is_after_update;       /* True if this boot is after an update */
    bool is_after_rollback;     /* True if bootloader performed rollback */
} ota_status_t;

/*============================================================================
 * FUNCTION PROTOTYPES
 *============================================================================*/

/**
 * Initialize OTA subsystem
 * Checks for firmware update/rollback status from bootloader
 */
void ota_init(void);

/**
 * Get current OTA status
 * @return Pointer to status structure (read-only)
 */
const ota_status_t* ota_get_status(void);

/**
 * Start a new firmware update
 * Initializes the download slot and prepares for receiving data
 *
 * @param total_size Expected total firmware size in bytes
 * @param expected_crc Optional expected CRC32 (0 to skip CRC check)
 * @return OTA_OK on success, error code on failure
 */
ota_error_t ota_begin(size_t total_size, uint32_t expected_crc);

/**
 * Write a chunk of firmware data
 * Data is buffered and written in 256-byte aligned blocks
 *
 * @param data Pointer to data buffer
 * @param len Length of data (can be any size)
 * @return OTA_OK on success, error code on failure
 */
ota_error_t ota_write_chunk(const uint8_t *data, size_t len);

/**
 * Finalize the firmware update
 * Validates SHA256 (if enabled), marks slot as valid
 *
 * @return OTA_OK on success, error code on failure
 */
ota_error_t ota_finish(void);

/**
 * Abort current update and reset state
 */
void ota_abort(void);

/**
 * Apply the update and reboot
 * This function does not return on success
 */
void ota_apply_and_reboot(void);

/**
 * Confirm successful boot (prevents rollback)
 * Should be called after the application has verified it's working correctly
 */
void ota_confirm_boot(void);

/**
 * Get human-readable error message
 * @param error Error code
 * @return Error message string
 */
const char* ota_error_str(ota_error_t error);

/**
 * Get human-readable state name
 * @param state State code
 * @return State name string
 */
const char* ota_state_str(ota_state_t state);

/**
 * Check if system is ready for OTA
 * @return true if ready, false if busy
 */
bool ota_is_ready(void);

/**
 * Get upload progress percentage
 * @return Progress 0-100
 */
uint8_t ota_get_progress(void);

#endif /* OTA_UPDATE_H */
