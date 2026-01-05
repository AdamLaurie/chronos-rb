/**
 * CHRONOS-Rb Configuration Storage
 *
 * Persistent configuration stored in flash memory.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "config.h"

/*============================================================================
 * FLASH STORAGE CONFIGURATION
 *============================================================================*/

/* Use the reserved filesystem area for config storage */
/* With 8KB reserved, config goes at 4MB - 8KB to avoid UF2 wraparound issue */
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - (8 * 1024))

/* Pointer to config in flash (read-only) */
#define FLASH_CONFIG_ADDR   (XIP_BASE + FLASH_TARGET_OFFSET)

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static config_t current_config;
static bool config_initialized = false;

/*============================================================================
 * CRC32 IMPLEMENTATION
 *============================================================================*/

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void crc32_init_table(void) {
    if (crc32_table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

static uint32_t crc32_compute(const void *data, size_t length) {
    crc32_init_table();

    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ bytes[i]) & 0xFF];
    }

    return crc ^ 0xFFFFFFFF;
}

/*============================================================================
 * PRIVATE FUNCTIONS
 *============================================================================*/

/**
 * Set default configuration values
 */
static void config_set_defaults(void) {
    memset(&current_config, 0, sizeof(current_config));
    current_config.magic = CONFIG_MAGIC;
    current_config.version = CONFIG_VERSION;
    current_config.wifi_enabled = false;
    current_config.wifi_ssid[0] = '\0';
    current_config.wifi_pass[0] = '\0';

    /* RF timecode outputs - all enabled by default */
    current_config.rf_dcf77_enabled = true;
    current_config.rf_wwvb_enabled = true;
    current_config.rf_jjy40_enabled = true;
    current_config.rf_jjy60_enabled = true;

    /* NMEA output - enabled by default */
    current_config.nmea_enabled = true;
}

/**
 * Validate configuration structure
 */
static bool config_validate(const config_t *cfg) {
    if (cfg->magic != CONFIG_MAGIC) {
        return false;
    }

    /* Accept current version or previous version for migration */
    if (cfg->version != CONFIG_VERSION && cfg->version != 1) {
        return false;
    }

    /* Compute CRC over everything except the CRC field itself */
    size_t crc_offset = offsetof(config_t, crc32);
    uint32_t computed_crc = crc32_compute(cfg, crc_offset);

    if (cfg->crc32 != computed_crc) {
        return false;
    }

    return true;
}

/**
 * Migrate config from older version to current version
 */
static void config_migrate(void) {
    if (current_config.version == 1) {
        printf("[CONFIG] Migrating from v1 to v2...\n");
        /* v1 -> v2: Add RF and NMEA settings with defaults */
        current_config.rf_dcf77_enabled = true;
        current_config.rf_wwvb_enabled = true;
        current_config.rf_jjy40_enabled = true;
        current_config.rf_jjy60_enabled = true;
        current_config.nmea_enabled = true;
        current_config.version = CONFIG_VERSION;
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize configuration system
 */
void config_init(void) {
    if (!config_load()) {
        printf("[CONFIG] No valid config found, using defaults\n");
        config_set_defaults();
    } else {
        printf("[CONFIG] Configuration loaded from flash\n");
        config_migrate();  /* Upgrade older config versions */
    }
    config_initialized = true;
}

/* Buffer for flash write - must be static so it persists during callback */
static uint8_t flash_write_buffer[FLASH_PAGE_SIZE];

/**
 * Flash write callback - called with interrupts disabled and safe to write flash
 */
static void flash_write_callback(void *param) {
    (void)param;

    /* Erase the sector first */
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

    /* Write the config */
    flash_range_program(FLASH_TARGET_OFFSET, flash_write_buffer, FLASH_PAGE_SIZE);
}

/**
 * Save current configuration to flash
 */
bool config_save(void) {
    /* Compute CRC before saving */
    size_t crc_offset = offsetof(config_t, crc32);
    current_config.crc32 = crc32_compute(&current_config, crc_offset);

    /* Prepare aligned buffer (flash writes require 256-byte alignment) */
    memset(flash_write_buffer, 0xFF, sizeof(flash_write_buffer));
    memcpy(flash_write_buffer, &current_config, sizeof(current_config));

    /* Use flash_safe_execute which properly handles WiFi/lwIP background tasks */
    int result = flash_safe_execute(flash_write_callback, NULL, UINT32_MAX);

    if (result != PICO_OK) {
        printf("[CONFIG] Flash write failed (error %d)\n", result);
        return false;
    }

    printf("[CONFIG] Configuration saved to flash\n");
    return true;
}

/**
 * Load configuration from flash
 */
bool config_load(void) {
    const config_t *flash_config = (const config_t *)FLASH_CONFIG_ADDR;

    if (!config_validate(flash_config)) {
        return false;
    }

    memcpy(&current_config, flash_config, sizeof(current_config));
    return true;
}

/**
 * Reset configuration to defaults
 */
void config_reset(void) {
    config_set_defaults();
    printf("[CONFIG] Configuration reset to defaults\n");
}

/**
 * Get pointer to current configuration
 */
config_t* config_get(void) {
    return &current_config;
}

/**
 * Set WiFi credentials
 */
void config_set_wifi(const char *ssid, const char *password, bool auto_connect) {
    if (ssid) {
        strncpy(current_config.wifi_ssid, ssid, CONFIG_SSID_MAX - 1);
        current_config.wifi_ssid[CONFIG_SSID_MAX - 1] = '\0';
    }

    if (password) {
        strncpy(current_config.wifi_pass, password, CONFIG_PASS_MAX - 1);
        current_config.wifi_pass[CONFIG_PASS_MAX - 1] = '\0';
    }

    current_config.wifi_enabled = auto_connect;
}

/**
 * Check if WiFi auto-connect is enabled
 */
bool config_wifi_auto_connect_enabled(void) {
    return current_config.wifi_enabled &&
           strlen(current_config.wifi_ssid) > 0;
}

/**
 * Print current configuration to console
 */
void config_print(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                  Current Configuration                       ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("WiFi Settings:\n");
    printf("  Auto-connect:   %s\n", current_config.wifi_enabled ? "Enabled" : "Disabled");
    if (strlen(current_config.wifi_ssid) > 0) {
        printf("  SSID:           %s\n", current_config.wifi_ssid);
        printf("  Password:       %s\n", strlen(current_config.wifi_pass) > 0 ? "********" : "(none)");
    } else {
        printf("  SSID:           (not configured)\n");
    }
    printf("\n");

    printf("Radio Timecode Outputs:\n");
    printf("  DCF77 (77.5kHz): %s\n", current_config.rf_dcf77_enabled ? "Enabled" : "Disabled");
    printf("  WWVB (60kHz):    %s\n", current_config.rf_wwvb_enabled ? "Enabled" : "Disabled");
    printf("  JJY40 (40kHz):   %s\n", current_config.rf_jjy40_enabled ? "Enabled" : "Disabled");
    printf("  JJY60 (60kHz):   %s\n", current_config.rf_jjy60_enabled ? "Enabled" : "Disabled");
    printf("\n");

    printf("Serial Outputs:\n");
    printf("  NMEA:            %s\n", current_config.nmea_enabled ? "Enabled" : "Disabled");
    printf("\n");

    printf("Config Info:\n");
    printf("  Magic:          0x%08lX %s\n", current_config.magic,
           current_config.magic == CONFIG_MAGIC ? "(valid)" : "(INVALID)");
    printf("  Version:        %lu\n", current_config.version);
    printf("\n");
}
