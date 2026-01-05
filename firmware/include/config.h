/**
 * CHRONOS-Rb Configuration Storage
 *
 * Persistent configuration stored in flash memory.
 * Uses the last sector of flash for non-volatile storage.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * CONFIGURATION STRUCTURE
 *============================================================================*/

#define CONFIG_MAGIC        0x4352424E  /* "CRBN" */
#define CONFIG_VERSION      3           /* Bumped for GPS settings */

#define CONFIG_SSID_MAX     33  /* 32 chars + null */
#define CONFIG_PASS_MAX     65  /* 64 chars + null */

typedef struct {
    uint32_t magic;                     /* Magic number for validation */
    uint32_t version;                   /* Config version */

    /* WiFi settings */
    bool wifi_enabled;                  /* Auto-connect on boot */
    char wifi_ssid[CONFIG_SSID_MAX];    /* WiFi SSID */
    char wifi_pass[CONFIG_PASS_MAX];    /* WiFi password */

    /* Radio timecode output settings */
    bool rf_dcf77_enabled;              /* DCF77 (Germany 77.5kHz) */
    bool rf_wwvb_enabled;               /* WWVB (USA 60kHz) */
    bool rf_jjy40_enabled;              /* JJY40 (Japan 40kHz) */
    bool rf_jjy60_enabled;              /* JJY60 (Japan 60kHz) */

    /* NMEA output settings */
    bool nmea_enabled;                  /* NMEA serial output */

    /* GPS receiver settings */
    bool gps_enabled;                   /* GPS receiver input enabled */

    /* Future expansion */
    uint8_t reserved[119];              /* Reserved for future use */

    uint32_t crc32;                     /* CRC32 checksum */
} config_t;

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize configuration system
 * Loads config from flash or sets defaults
 */
void config_init(void);

/**
 * Save current configuration to flash
 * @return true on success
 */
bool config_save(void);

/**
 * Load configuration from flash
 * @return true if valid config found
 */
bool config_load(void);

/**
 * Reset configuration to defaults
 */
void config_reset(void);

/**
 * Get pointer to current configuration
 * @return Pointer to config structure
 */
config_t* config_get(void);

/**
 * Set WiFi credentials
 * @param ssid WiFi SSID (max 32 chars)
 * @param password WiFi password (max 64 chars)
 * @param auto_connect Enable auto-connect on boot
 */
void config_set_wifi(const char *ssid, const char *password, bool auto_connect);

/**
 * Check if WiFi auto-connect is enabled
 * @return true if auto-connect enabled with valid credentials
 */
bool config_wifi_auto_connect_enabled(void);

/**
 * Print current configuration to console
 */
void config_print(void);

#endif /* CONFIG_H */
