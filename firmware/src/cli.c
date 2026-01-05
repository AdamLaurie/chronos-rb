/**
 * CHRONOS-Rb Command Line Interface
 *
 * USB UART CLI for device configuration and status
 *
 * Commands:
 *   help              - Show available commands
 *   status            - Show system status
 *   reboot            - Reboot the device
 *   reboot bl         - Reboot into bootloader mode
 *   wifi <SSID> <PWD> - Set WiFi (quote args with spaces)
 *   pulse ...         - Configure GPIO pulse outputs
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

#include "chronos_rb.h"
#include "cli.h"
#include "pulse_output.h"
#include "ac_freq_monitor.h"
#include "config.h"
#include "radio_timecode.h"
#include "nmea_output.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define CLI_BUFFER_SIZE     128
#define CLI_MAX_ARGS        8
#define CLI_PROMPT          "chronos> "

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static char cli_buffer[CLI_BUFFER_SIZE];
static uint32_t cli_buffer_pos = 0;
static bool cli_initialized = false;

/* Output buffer for web CLI (NULL = use printf) */
static char *cli_out_buf = NULL;
static size_t cli_out_len = 0;
static int cli_out_pos = 0;

/*============================================================================
 * OUTPUT FUNCTIONS
 *============================================================================*/

/**
 * CLI printf - outputs to buffer if set, otherwise to stdout
 */
static int cli_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret;

    if (cli_out_buf != NULL) {
        /* Output to buffer */
        ret = vsnprintf(cli_out_buf + cli_out_pos, cli_out_len - cli_out_pos, fmt, args);
        if (ret > 0) {
            cli_out_pos += ret;
            if (cli_out_pos >= (int)cli_out_len) {
                cli_out_pos = cli_out_len - 1;
            }
        }
    } else {
        /* Output to stdout */
        ret = vprintf(fmt, args);
    }

    va_end(args);
    return ret;
}

/*============================================================================
 * HELPER FUNCTIONS
 *============================================================================*/

/**
 * Trim leading and trailing whitespace
 */
static char *trim(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

/**
 * Parse command line into arguments (supports quoted strings)
 */
static int parse_args(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Check for quoted argument */
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;
            /* Find closing quote */
            while (*p && *p != quote) p++;
            if (*p) *p++ = '\0';
        } else {
            /* Unquoted argument */
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
        }
    }

    return argc;
}

/**
 * Parse HH:MM time string
 */
static bool parse_time(const char *str, uint8_t *hour, uint8_t *minute) {
    int h, m;
    if (sscanf(str, "%d:%d", &h, &m) != 2) {
        return false;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    *hour = (uint8_t)h;
    *minute = (uint8_t)m;
    return true;
}

/*============================================================================
 * COMMAND HANDLERS
 *============================================================================*/

/**
 * Show help
 */
static void cmd_help(void) {
    cli_printf("\nCHRONOS-Rb CLI Commands:\n");
    cli_printf("  help                - Show this help message\n");
    cli_printf("  status              - Show system status\n");
    cli_printf("  pins                - Show GPIO pin assignments\n");
    cli_printf("  acfreq              - Show AC mains frequency\n");
    cli_printf("  debug on|off        - Enable/disable periodic debug output\n");
    cli_printf("  config show         - Show current configuration\n");
    cli_printf("  config save         - Save configuration to flash\n");
    cli_printf("  config reset        - Reset configuration to defaults\n");
    cli_printf("  reboot              - Reboot the device\n");
    cli_printf("  reboot bl           - Reboot into USB bootloader\n");
    cli_printf("  wifi <SSID> <PWD>   - Connect to WiFi (quote SSID if spaces)\n");
    cli_printf("\n");
    cli_printf("Pulse Output Commands:\n");
    cli_printf("  pulse <pin> P <interval_sec> <width_ms>\n");
    cli_printf("                      - Pulse every N seconds\n");
    cli_printf("  pulse <pin> S <second> <width_ms> <count> <gap_ms>\n");
    cli_printf("                      - Burst on specific second (0-59) each minute\n");
    cli_printf("  pulse <pin> M <minute> <width_ms> <count> <gap_ms>\n");
    cli_printf("                      - Burst on specific minute (0-59) each hour\n");
    cli_printf("  pulse <pin> H <HH:MM> <width_ms> <count> <gap_ms>\n");
    cli_printf("                      - Burst at specific time each day\n");
    cli_printf("  pulse <pin> off     - Disable pulse output\n");
    cli_printf("  pulse list          - List all pulse configurations\n");
    cli_printf("  pulse clear         - Clear all pulse configurations\n");
    cli_printf("\n");
    cli_printf("  count  = number of pulses in burst (1 = single)\n");
    cli_printf("  gap_ms = gap between pulses in burst (ms)\n");
    cli_printf("\n");
    cli_printf("Examples:\n");
    cli_printf("  pulse 14 P 10 300       - GPIO14 pulse every 10s, 300ms\n");
    cli_printf("  pulse 15 S 0 100 1 0    - GPIO15 single 100ms pulse on second 0\n");
    cli_printf("  pulse 16 M 59 50 5 100  - GPIO16 5x50ms pulses (100ms gap) on min 59\n");
    cli_printf("  pulse 17 H 00:00 500 3 200 - GPIO17 3x500ms (200ms gap) at midnight\n");
    cli_printf("\n");
    cli_printf("Radio Timecode Commands:\n");
    cli_printf("  rf                        - Show RF output status\n");
    cli_printf("  rf <signal> <on|off>      - Enable/disable output\n");
    cli_printf("  Signals: dcf77, wwvb, jjy40, jjy60, all\n");
    cli_printf("\n");
    cli_printf("NMEA Output:\n");
    cli_printf("  nmea                      - Show NMEA status\n");
    cli_printf("  nmea <on|off>             - Enable/disable NMEA output\n");
    cli_printf("\n");
}

/**
 * Show system status
 */
static void cmd_status(void) {
    const char *sync_states[] = {
        "INIT", "FREQ_CAL", "COARSE", "FINE", "LOCKED", "HOLDOVER", "ERROR"
    };

    cli_printf("\n");
    cli_printf("╔══════════════════════════════════════════════════════════════╗\n");
    cli_printf("║                    CHRONOS-Rb Status                         ║\n");
    cli_printf("╚══════════════════════════════════════════════════════════════╝\n");
    cli_printf("\n");

    /* Sync Status */
    cli_printf("Synchronization:\n");
    cli_printf("  State:          %s\n", sync_states[g_time_state.sync_state]);
    cli_printf("  Rb Lock:        %s\n", g_time_state.rb_locked ? "YES" : "NO");
    cli_printf("  Time Valid:     %s\n", g_time_state.time_valid ? "YES" : "NO");
    cli_printf("  PPS Count:      %lu\n", g_time_state.pps_count);
    cli_printf("\n");

    /* Timing */
    cli_printf("Timing:\n");
    cli_printf("  Offset:         %lld ns\n", g_time_state.offset_ns);
    cli_printf("  Freq Offset:    %.3f ppb\n", g_time_state.frequency_offset);
    cli_printf("  Freq Count:     %lu Hz\n", g_time_state.last_freq_count);
    cli_printf("\n");

    /* Network */
    cli_printf("Network:\n");
    cli_printf("  WiFi:           %s\n", g_wifi_connected ? "Connected" : "Disconnected");
    if (g_wifi_connected) {
        char ip_str[16];
        get_ip_address_str(ip_str, sizeof(ip_str));
        cli_printf("  IP Address:     %s\n", ip_str);
    }
    cli_printf("\n");

    /* Statistics */
    cli_printf("Statistics:\n");
    cli_printf("  NTP Requests:   %lu\n", g_stats.ntp_requests);
    cli_printf("  PTP Sync Sent:  %lu\n", g_stats.ptp_sync_sent);
    cli_printf("  Errors:         %lu\n", g_stats.errors);
    cli_printf("  Min Offset:     %ld ns\n", g_stats.min_offset_ns);
    cli_printf("  Max Offset:     %ld ns\n", g_stats.max_offset_ns);
    cli_printf("  Avg Offset:     %.1f ns\n", g_stats.avg_offset_ns);
    cli_printf("\n");

    /* AC Mains Frequency */
    cli_printf("AC Mains:\n");
    if (ac_freq_signal_present()) {
        cli_printf("  Frequency:      %.3f Hz\n", ac_freq_get_hz());
        cli_printf("  Average:        %.3f Hz\n", ac_freq_get_avg_hz());
    } else {
        cli_printf("  Signal:         Not detected\n");
    }
    cli_printf("\n");
}

/**
 * Show GPIO pin assignments
 */
static void cmd_pins(void) {
    cli_printf("\n");
    cli_printf("╔══════════════════════════════════════════════════════════════╗\n");
    cli_printf("║                  GPIO Pin Assignments                        ║\n");
    cli_printf("╚══════════════════════════════════════════════════════════════╝\n");
    cli_printf("\n");

    cli_printf("Inputs:\n");
    cli_printf("  GP%-2d  FE-5680A 1PPS      From external FE-5680A\n", GPIO_FE_PPS_INPUT);
    cli_printf("  GP%-2d  FE-5680A 10MHz    From comparator circuit\n", GPIO_FE_10MHZ_INPUT);
    cli_printf("  GP%-2d  Rb Lock Status     HIGH=locked (FE-5680A pin 3 via NPN)\n", GPIO_RB_LOCK_STATUS);
    cli_printf("  GP%-2d  AC Zero-Cross      Mains frequency monitor\n", GPIO_AC_ZERO_CROSS);
    cli_printf("\n");

    cli_printf("Outputs - Status LEDs:\n");
    cli_printf("  GP%-2d  LED Sync           Green - Synchronized to Rb\n", GPIO_LED_SYNC);
    cli_printf("  GP%-2d  LED Network        Blue - WiFi connected\n", GPIO_LED_NETWORK);
    cli_printf("  GP%-2d  LED Activity       Yellow - NTP/PTP activity\n", GPIO_LED_ACTIVITY);
    cli_printf("  GP%-2d  LED Error          Red - Error condition\n", GPIO_LED_ERROR);
    cli_printf("\n");

    cli_printf("Outputs - Debug:\n");
    cli_printf("  GP%-2d  Debug PPS Out      Regenerated 1PPS for test\n", GPIO_DEBUG_PPS_OUT);
    cli_printf("  GP%-2d  Debug Sync Pulse   Sync pulse indicator\n", GPIO_DEBUG_SYNC_PULSE);
    cli_printf("\n");

    cli_printf("Outputs - Fixed Interval Pulses:\n");
    cli_printf("  GP%-2d  Pulse 0.5s         500ms interval\n", GPIO_PULSE_500MS);
    cli_printf("  GP%-2d  Pulse 1s           1 second interval\n", GPIO_PULSE_1S);
    cli_printf("  GP%-2d  Pulse 6s           6 second interval\n", GPIO_PULSE_6S);
    cli_printf("  GP%-2d  Pulse 30s          30 second interval\n", GPIO_PULSE_30S);
    cli_printf("  GP%-2d  Pulse 60s          60 second interval\n", GPIO_PULSE_60S);
    cli_printf("\n");

    cli_printf("Peripherals:\n");
    cli_printf("  GP%-2d  UART TX            Debug serial output\n", GPIO_UART_TX);
    cli_printf("  GP%-2d  UART RX            Debug serial input\n", GPIO_UART_RX);
    cli_printf("  GP%-2d  I2C SDA            Optional OLED display\n", GPIO_I2C_SDA);
    cli_printf("  GP%-2d  I2C SCL            Optional OLED display\n", GPIO_I2C_SCL);
    cli_printf("\n");

    cli_printf("Control:\n");
    cli_printf("  GP%-2d  Rb Enable          Optional FE-5680A enable\n", GPIO_RB_ENABLE);
    cli_printf("\n");
}

/**
 * Debug output control
 */
static void cmd_debug(int argc, char **argv) {
    if (argc < 2) {
        cli_printf("Debug output: %s\n", g_debug_enabled ? "ON" : "OFF");
        cli_printf("Usage: debug on|off\n");
        return;
    }

    if (strcmp(argv[1], "on") == 0) {
        g_debug_enabled = true;
        cli_printf("Debug output enabled\n");
    } else if (strcmp(argv[1], "off") == 0) {
        g_debug_enabled = false;
        cli_printf("Debug output disabled\n");
    } else {
        cli_printf("Usage: debug on|off\n");
    }
}

/**
 * Reboot device
 */
static void cmd_reboot(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "bl") == 0) {
        cli_printf("Rebooting into USB bootloader...\n");
        sleep_ms(100);
        reset_usb_boot(0, 0);
    } else {
        cli_printf("Rebooting...\n");
        sleep_ms(100);
        watchdog_reboot(0, 0, 0);
    }
}

/**
 * Set WiFi credentials
 */
static void cmd_wifi(int argc, char **argv) {
    if (argc < 3) {
        cli_printf("Usage: wifi <SSID> <PASSWORD>\n");
        cli_printf("  Use quotes for SSID/password with spaces:\n");
        cli_printf("    wifi \"My Network\" \"my password\"\n");
        cli_printf("  Credentials are saved for auto-connect on reboot\n");
        return;
    }

    const char *ssid = argv[1];
    const char *password = argv[2];

    if (strlen(ssid) > 32) {
        cli_printf("Error: SSID too long (max 32 characters)\n");
        return;
    }

    if (strlen(password) > 64) {
        cli_printf("Error: Password too long (max 64 characters)\n");
        return;
    }

    cli_printf("Connecting to '%s'...\n", ssid);

    /* Extend watchdog timeout during blocking wifi connect (10s timeout) */
    watchdog_enable(15000, 1);

    if (wifi_connect(ssid, password)) {
        /* Restore normal watchdog timeout */
        watchdog_enable(8000, 1);
        cli_printf("Connected successfully!\n");

        char ip_str[16];
        get_ip_address_str(ip_str, sizeof(ip_str));
        cli_printf("IP Address: %s\n", ip_str);

        /* Save credentials for auto-connect */
        config_set_wifi(ssid, password, true);
        config_save();
        cli_printf("Credentials saved for auto-connect\n");

        /* Start network services */
        ntp_server_init();
        ptp_server_init();
        web_init();
        cli_printf("Network services started\n");
    } else {
        /* Restore normal watchdog timeout */
        watchdog_enable(8000, 1);
        printf("Connection failed!\n");
    }
}

/**
 * Configuration commands
 */
static void cmd_config(int argc, char **argv) {
    if (argc < 2) {
        cli_printf("Usage: config <show|save|reset>\n");
        return;
    }

    if (strcmp(argv[1], "show") == 0) {
        config_print();
    } else if (strcmp(argv[1], "save") == 0) {
        config_save();
        cli_printf("Configuration saved to flash\n");
    } else if (strcmp(argv[1], "reset") == 0) {
        config_reset();
        cli_printf("Configuration reset to defaults\n");
        cli_printf("Use 'config save' to persist, or 'reboot' to discard\n");
    } else {
        cli_printf("Unknown config command: %s\n", argv[1]);
        cli_printf("Valid commands: show, save, reset\n");
    }
}

/**
 * Configure pulse outputs
 */
static void cmd_pulse(int argc, char **argv) {
    if (argc < 2) {
        cli_printf("Usage: pulse <pin> <mode> <params...>\n");
        cli_printf("       pulse list | clear\n");
        cli_printf("Type 'help' for full syntax\n");
        return;
    }

    /* Handle list and clear */
    if (strcmp(argv[1], "list") == 0) {
        pulse_output_list();
        return;
    }

    if (strcmp(argv[1], "clear") == 0) {
        pulse_output_clear_all();
        return;
    }

    /* Parse GPIO pin */
    int pin = atoi(argv[1]);
    if (pin < 0 || pin > 28) {
        cli_printf("Error: Invalid GPIO pin (0-28)\n");
        return;
    }

    if (argc < 3) {
        cli_printf("Error: Missing mode (P/S/M/H/off)\n");
        return;
    }

    /* Handle off */
    if (strcasecmp(argv[2], "off") == 0) {
        pulse_output_disable((uint8_t)pin);
        return;
    }

    /* Parse mode */
    char mode = toupper((unsigned char)argv[2][0]);

    switch (mode) {
        case 'P': {
            /* Interval mode: pulse <pin> P <interval> <width> */
            if (argc < 5) {
                cli_printf("Usage: pulse <pin> P <interval_sec> <width_ms>\n");
                return;
            }
            uint32_t interval = (uint32_t)atoi(argv[3]);
            uint16_t width = (uint16_t)atoi(argv[4]);
            pulse_output_set_interval((uint8_t)pin, interval, width);
            break;
        }

        case 'S': {
            /* Second mode: pulse <pin> S <second> <width> <count> <gap> */
            if (argc < 7) {
                cli_printf("Usage: pulse <pin> S <second> <width_ms> <count> <gap_ms>\n");
                return;
            }
            uint8_t second = (uint8_t)atoi(argv[3]);
            uint16_t width = (uint16_t)atoi(argv[4]);
            uint16_t count = (uint16_t)atoi(argv[5]);
            uint16_t gap = (uint16_t)atoi(argv[6]);
            pulse_output_set_second((uint8_t)pin, second, width, count, gap);
            break;
        }

        case 'M': {
            /* Minute mode: pulse <pin> M <minute> <width> <count> <gap> */
            if (argc < 7) {
                cli_printf("Usage: pulse <pin> M <minute> <width_ms> <count> <gap_ms>\n");
                return;
            }
            uint8_t minute = (uint8_t)atoi(argv[3]);
            uint16_t width = (uint16_t)atoi(argv[4]);
            uint16_t count = (uint16_t)atoi(argv[5]);
            uint16_t gap = (uint16_t)atoi(argv[6]);
            pulse_output_set_minute((uint8_t)pin, minute, width, count, gap);
            break;
        }

        case 'H': {
            /* Time mode: pulse <pin> H <HH:MM> <width> <count> <gap> */
            if (argc < 7) {
                cli_printf("Usage: pulse <pin> H <HH:MM> <width_ms> <count> <gap_ms>\n");
                return;
            }
            uint8_t hour, minute;
            if (!parse_time(argv[3], &hour, &minute)) {
                cli_printf("Error: Invalid time format (use HH:MM)\n");
                return;
            }
            uint16_t width = (uint16_t)atoi(argv[4]);
            uint16_t count = (uint16_t)atoi(argv[5]);
            uint16_t gap = (uint16_t)atoi(argv[6]);
            pulse_output_set_time((uint8_t)pin, hour, minute, width, count, gap);
            break;
        }

        default:
            cli_printf("Error: Unknown mode '%c'\n", mode);
            cli_printf("Valid modes: P (interval), S (second), M (minute), H (time), off\n");
            break;
    }
}

/**
 * Radio timecode output control
 */
static void cmd_rf(int argc, char **argv) {
    config_t *cfg = config_get();

    if (argc < 2) {
        cli_printf("Radio Timecode Status:\n");
        cli_printf("  DCF77  (GP%d, 77.5kHz):  %s\n", GPIO_DCF77,
                   radio_timecode_is_enabled(RADIO_DCF77) ? "ON" : "OFF");
        cli_printf("  WWVB   (GP%d,   60kHz):  %s\n", GPIO_WWVB,
                   radio_timecode_is_enabled(RADIO_WWVB) ? "ON" : "OFF");
        cli_printf("  JJY40  (GP%d,   40kHz):  %s\n", GPIO_JJY40,
                   radio_timecode_is_enabled(RADIO_JJY40) ? "ON" : "OFF");
        cli_printf("  JJY60  (GP%d,   60kHz): %s\n", GPIO_JJY60,
                   radio_timecode_is_enabled(RADIO_JJY60) ? "ON" : "OFF");
        cli_printf("\nUsage: rf <dcf77|wwvb|jjy40|jjy60|all> <on|off>\n");
        return;
    }

    if (argc < 3) {
        cli_printf("Usage: rf <dcf77|wwvb|jjy40|jjy60|all> <on|off>\n");
        return;
    }

    const char *signal = argv[1];
    bool enable = (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "1") == 0);

    if (strcmp(signal, "dcf77") == 0) {
        radio_timecode_enable(RADIO_DCF77, enable);
        cfg->rf_dcf77_enabled = enable;
        cli_printf("DCF77 %s\n", enable ? "enabled" : "disabled");
    } else if (strcmp(signal, "wwvb") == 0) {
        radio_timecode_enable(RADIO_WWVB, enable);
        cfg->rf_wwvb_enabled = enable;
        cli_printf("WWVB %s\n", enable ? "enabled" : "disabled");
    } else if (strcmp(signal, "jjy40") == 0) {
        radio_timecode_enable(RADIO_JJY40, enable);
        cfg->rf_jjy40_enabled = enable;
        cli_printf("JJY40 %s\n", enable ? "enabled" : "disabled");
    } else if (strcmp(signal, "jjy60") == 0) {
        radio_timecode_enable(RADIO_JJY60, enable);
        cfg->rf_jjy60_enabled = enable;
        cli_printf("JJY60 %s\n", enable ? "enabled" : "disabled");
    } else if (strcmp(signal, "all") == 0) {
        radio_timecode_enable(RADIO_DCF77, enable);
        radio_timecode_enable(RADIO_WWVB, enable);
        radio_timecode_enable(RADIO_JJY40, enable);
        radio_timecode_enable(RADIO_JJY60, enable);
        cfg->rf_dcf77_enabled = enable;
        cfg->rf_wwvb_enabled = enable;
        cfg->rf_jjy40_enabled = enable;
        cfg->rf_jjy60_enabled = enable;
        cli_printf("All RF outputs %s\n", enable ? "enabled" : "disabled");
    } else {
        cli_printf("Unknown signal: %s\n", signal);
        cli_printf("Valid signals: dcf77, wwvb, jjy40, jjy60, all\n");
        return;
    }

    cli_printf("Use 'config save' to persist settings\n");
}

/**
 * NMEA output control
 */
static void cmd_nmea(int argc, char **argv) {
    config_t *cfg = config_get();

    if (argc < 2) {
        cli_printf("NMEA Output: %s (GP%d)\n",
                   nmea_output_is_enabled() ? "ON" : "OFF", GPIO_NMEA_TX);
        cli_printf("Usage: nmea <on|off>\n");
        return;
    }

    bool enable = (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0);
    nmea_output_enable(enable);
    cfg->nmea_enabled = enable;
    cli_printf("NMEA %s\n", enable ? "enabled" : "disabled");
    cli_printf("Use 'config save' to persist settings\n");
}

/*============================================================================
 * COMMAND PROCESSOR
 *============================================================================*/

/**
 * Process a complete command line
 */
static void process_command(char *line) {
    char *trimmed = trim(line);

    if (strlen(trimmed) == 0) {
        return;
    }

    char *argv[CLI_MAX_ARGS];
    int argc = parse_args(trimmed, argv, CLI_MAX_ARGS);

    if (argc == 0) {
        return;
    }

    /* Match command */
    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "status") == 0) {
        cmd_status();
    } else if (strcmp(argv[0], "pins") == 0) {
        cmd_pins();
    } else if (strcmp(argv[0], "acfreq") == 0) {
        ac_freq_print_status();
    } else if (strcmp(argv[0], "debug") == 0) {
        cmd_debug(argc, argv);
    } else if (strcmp(argv[0], "config") == 0) {
        cmd_config(argc, argv);
    } else if (strcmp(argv[0], "reboot") == 0) {
        cmd_reboot(argc, argv);
    } else if (strcmp(argv[0], "wifi") == 0) {
        cmd_wifi(argc, argv);
    } else if (strcmp(argv[0], "pulse") == 0) {
        cmd_pulse(argc, argv);
    } else if (strcmp(argv[0], "rf") == 0) {
        cmd_rf(argc, argv);
    } else if (strcmp(argv[0], "nmea") == 0) {
        cmd_nmea(argc, argv);
    } else {
        cli_printf("Unknown command: %s\n", argv[0]);
        cli_printf("Type 'help' for available commands\n");
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize CLI
 */
void cli_init(void) {
    cli_buffer_pos = 0;
    memset(cli_buffer, 0, sizeof(cli_buffer));
    cli_initialized = true;

    printf("\nType 'help' for available commands\n");
    printf(CLI_PROMPT);
}

/**
 * Process CLI input - call from main loop
 */
void cli_task(void) {
    if (!cli_initialized) {
        return;
    }

    int c = getchar_timeout_us(0);

    if (c == PICO_ERROR_TIMEOUT) {
        return;
    }

    if (c == '\r' || c == '\n') {
        /* End of line - process command */
        printf("\n");
        cli_buffer[cli_buffer_pos] = '\0';
        process_command(cli_buffer);
        cli_buffer_pos = 0;
        printf(CLI_PROMPT);
    } else if (c == '\b' || c == 127) {
        /* Backspace */
        if (cli_buffer_pos > 0) {
            cli_buffer_pos--;
            printf("\b \b");
        }
    } else if (c == 0x03) {
        /* Ctrl+C - cancel current line */
        printf("^C\n");
        cli_buffer_pos = 0;
        printf(CLI_PROMPT);
    } else if (c >= 32 && c < 127) {
        /* Printable character */
        if (cli_buffer_pos < CLI_BUFFER_SIZE - 1) {
            cli_buffer[cli_buffer_pos++] = (char)c;
            putchar(c);
        }
    }
}

/**
 * Execute a CLI command and capture output to buffer
 */
int cli_execute(const char *cmd, char *out_buf, size_t out_len) {
    if (out_buf == NULL || out_len == 0) {
        /* No buffer - just execute with printf output */
        char cmd_copy[CLI_BUFFER_SIZE];
        strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';
        process_command(cmd_copy);
        return 0;
    }

    /* Set up output buffer */
    cli_out_buf = out_buf;
    cli_out_len = out_len;
    cli_out_pos = 0;
    out_buf[0] = '\0';

    /* Copy and process command */
    char cmd_copy[CLI_BUFFER_SIZE];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *trimmed = trim(cmd_copy);
    if (strlen(trimmed) > 0) {
        process_command(trimmed);
    }

    /* Clear output buffer pointer */
    int result = cli_out_pos;
    cli_out_buf = NULL;
    cli_out_len = 0;
    cli_out_pos = 0;

    return result;
}
