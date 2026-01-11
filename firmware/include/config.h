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
#define CONFIG_VERSION      4           /* Bumped for pulse output storage */

#define CONFIG_SSID_MAX     33  /* 32 chars + null */
#define CONFIG_PASS_MAX     65  /* 64 chars + null */

/* Pulse output configuration (stored version - no runtime state) */
#define CONFIG_MAX_PULSE_OUTPUTS   8

typedef struct __attribute__((packed)) {
    uint8_t gpio_pin;           /* GPIO pin number */
    uint8_t mode;               /* pulse_mode_t as uint8_t */
    uint8_t trigger_second;     /* Second to trigger (0-59) */
    uint8_t trigger_minute;     /* Minute to trigger (0-59) */
    uint8_t trigger_hour;       /* Hour to trigger (0-23) */
    uint8_t active;             /* Configuration is active */
    uint16_t pulse_width_ms;    /* Pulse width in milliseconds */
    uint16_t pulse_count;       /* Number of pulses in burst */
    uint16_t pulse_gap_ms;      /* Gap between pulses in burst (ms) */
    uint16_t interval_ds;       /* Interval in deciseconds (0.1s units, max 6553.5s) */
} pulse_config_stored_t;        /* 14 bytes per config */

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

    /* GNSS receiver settings */
    bool gnss_enabled;                  /* GNSS receiver input enabled */

    /* Pulse output configurations (8 slots × 14 bytes = 112 bytes) */
    pulse_config_stored_t pulse_configs[CONFIG_MAX_PULSE_OUTPUTS];

    /* Future expansion */
    uint8_t reserved[7];                /* Reserved for future use */

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

/**
 * Get pulse configuration array
 * @return Pointer to pulse config array (8 elements)
 */
pulse_config_stored_t* config_get_pulse_configs(void);

/**
 * Update pulse configuration at given index
 * @param index Pulse slot index (0-7)
 * @param cfg Pulse configuration to store
 */
void config_set_pulse_config(int index, const pulse_config_stored_t *cfg);

/**
 * Clear all pulse configurations
 */
void config_clear_pulse_configs(void);

#endif /* CONFIG_H */
