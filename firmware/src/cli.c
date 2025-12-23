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
 *   wifi <SSID> <PWD> - Set WiFi credentials and connect
 *   pulse ...         - Configure GPIO pulse outputs
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"

#include "chronos_rb.h"
#include "cli.h"
#include "pulse_output.h"
#include "ac_freq_monitor.h"
#include "config.h"

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
 * Parse command line into arguments
 */
static int parse_args(char *line, char **argv, int max_args) {
    int argc = 0;
    char *token = strtok(line, " \t");

    while (token != NULL && argc < max_args) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
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
    printf("\nCHRONOS-Rb CLI Commands:\n");
    printf("  help                - Show this help message\n");
    printf("  status              - Show system status\n");
    printf("  pins                - Show GPIO pin assignments\n");
    printf("  acfreq              - Show AC mains frequency\n");
    printf("  debug on|off        - Enable/disable periodic debug output\n");
    printf("  config show         - Show current configuration\n");
    printf("  config save         - Save configuration to flash\n");
    printf("  config reset        - Reset configuration to defaults\n");
    printf("  reboot              - Reboot the device\n");
    printf("  reboot bl           - Reboot into USB bootloader\n");
    printf("  wifi <SSID> <PWD>   - Connect to WiFi and save credentials\n");
    printf("\n");
    printf("Pulse Output Commands:\n");
    printf("  pulse <pin> P <interval_sec> <width_ms>\n");
    printf("                      - Pulse every N seconds\n");
    printf("  pulse <pin> S <second> <width_ms> <count> <gap_ms>\n");
    printf("                      - Burst on specific second (0-59) each minute\n");
    printf("  pulse <pin> M <minute> <width_ms> <count> <gap_ms>\n");
    printf("                      - Burst on specific minute (0-59) each hour\n");
    printf("  pulse <pin> H <HH:MM> <width_ms> <count> <gap_ms>\n");
    printf("                      - Burst at specific time each day\n");
    printf("  pulse <pin> off     - Disable pulse output\n");
    printf("  pulse list          - List all pulse configurations\n");
    printf("  pulse clear         - Clear all pulse configurations\n");
    printf("\n");
    printf("  count  = number of pulses in burst (1 = single)\n");
    printf("  gap_ms = gap between pulses in burst (ms)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  pulse 14 P 10 300       - GPIO14 pulse every 10s, 300ms\n");
    printf("  pulse 15 S 0 100 1 0    - GPIO15 single 100ms pulse on second 0\n");
    printf("  pulse 16 M 59 50 5 100  - GPIO16 5x50ms pulses (100ms gap) on min 59\n");
    printf("  pulse 17 H 00:00 500 3 200 - GPIO17 3x500ms (200ms gap) at midnight\n");
    printf("\n");
}

/**
 * Show system status
 */
static void cmd_status(void) {
    const char *sync_states[] = {
        "INIT", "FREQ_CAL", "COARSE", "FINE", "LOCKED", "HOLDOVER", "ERROR"
    };

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    CHRONOS-Rb Status                         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Sync Status */
    printf("Synchronization:\n");
    printf("  State:          %s\n", sync_states[g_time_state.sync_state]);
    printf("  Rb Lock:        %s\n", g_time_state.rb_locked ? "YES" : "NO");
    printf("  Time Valid:     %s\n", g_time_state.time_valid ? "YES" : "NO");
    printf("  PPS Count:      %lu\n", g_time_state.pps_count);
    printf("\n");

    /* Timing */
    printf("Timing:\n");
    printf("  Offset:         %lld ns\n", g_time_state.offset_ns);
    printf("  Freq Offset:    %.3f ppb\n", g_time_state.frequency_offset);
    printf("  Freq Count:     %lu Hz\n", g_time_state.last_freq_count);
    printf("\n");

    /* Network */
    printf("Network:\n");
    printf("  WiFi:           %s\n", g_wifi_connected ? "Connected" : "Disconnected");
    if (g_wifi_connected) {
        char ip_str[16];
        get_ip_address_str(ip_str, sizeof(ip_str));
        printf("  IP Address:     %s\n", ip_str);
    }
    printf("\n");

    /* Statistics */
    printf("Statistics:\n");
    printf("  NTP Requests:   %lu\n", g_stats.ntp_requests);
    printf("  PTP Sync Sent:  %lu\n", g_stats.ptp_sync_sent);
    printf("  Errors:         %lu\n", g_stats.errors);
    printf("  Min Offset:     %ld ns\n", g_stats.min_offset_ns);
    printf("  Max Offset:     %ld ns\n", g_stats.max_offset_ns);
    printf("  Avg Offset:     %.1f ns\n", g_stats.avg_offset_ns);
    printf("\n");

    /* AC Mains Frequency */
    printf("AC Mains:\n");
    if (ac_freq_signal_present()) {
        printf("  Frequency:      %.3f Hz\n", ac_freq_get_hz());
        printf("  Average:        %.3f Hz\n", ac_freq_get_avg_hz());
    } else {
        printf("  Signal:         Not detected\n");
    }
    printf("\n");
}

/**
 * Show GPIO pin assignments
 */
static void cmd_pins(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                  GPIO Pin Assignments                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Inputs:\n");
    printf("  GP%-2d  1PPS Input         From level shifter (FE-5680A)\n", GPIO_PPS_INPUT);
    printf("  GP%-2d  10MHz Input        From comparator (FE-5680A)\n", GPIO_10MHZ_INPUT);
    printf("  GP%-2d  Rb Lock Status     Active LOW (FE-5680A pin 9)\n", GPIO_RB_LOCK_STATUS);
    printf("  GP%-2d  AC Zero-Cross      Mains frequency monitor\n", GPIO_AC_ZERO_CROSS);
    printf("\n");

    printf("Outputs - Status LEDs:\n");
    printf("  GP%-2d  LED Sync           Green - Synchronized to Rb\n", GPIO_LED_SYNC);
    printf("  GP%-2d  LED Network        Blue - WiFi connected\n", GPIO_LED_NETWORK);
    printf("  GP%-2d  LED Activity       Yellow - NTP/PTP activity\n", GPIO_LED_ACTIVITY);
    printf("  GP%-2d  LED Error          Red - Error condition\n", GPIO_LED_ERROR);
    printf("\n");

    printf("Outputs - Debug:\n");
    printf("  GP%-2d  Debug PPS Out      Regenerated 1PPS for test\n", GPIO_DEBUG_PPS_OUT);
    printf("  GP%-2d  Debug Sync Pulse   Sync pulse indicator\n", GPIO_DEBUG_SYNC_PULSE);
    printf("\n");

    printf("Outputs - Fixed Interval Pulses:\n");
    printf("  GP%-2d  Pulse 0.5s         500ms interval\n", GPIO_PULSE_500MS);
    printf("  GP%-2d  Pulse 1s           1 second interval\n", GPIO_PULSE_1S);
    printf("  GP%-2d  Pulse 6s           6 second interval\n", GPIO_PULSE_6S);
    printf("  GP%-2d  Pulse 30s          30 second interval\n", GPIO_PULSE_30S);
    printf("  GP%-2d  Pulse 60s          60 second interval\n", GPIO_PULSE_60S);
    printf("\n");

    printf("Peripherals:\n");
    printf("  GP%-2d  UART TX            Debug serial output\n", GPIO_UART_TX);
    printf("  GP%-2d  UART RX            Debug serial input\n", GPIO_UART_RX);
    printf("  GP%-2d  I2C SDA            Optional OLED display\n", GPIO_I2C_SDA);
    printf("  GP%-2d  I2C SCL            Optional OLED display\n", GPIO_I2C_SCL);
    printf("\n");

    printf("Control:\n");
    printf("  GP%-2d  Rb Enable          Optional FE-5680A enable\n", GPIO_RB_ENABLE);
    printf("\n");
}

/**
 * Debug output control
 */
static void cmd_debug(int argc, char **argv) {
    if (argc < 2) {
        printf("Debug output: %s\n", g_debug_enabled ? "ON" : "OFF");
        printf("Usage: debug on|off\n");
        return;
    }

    if (strcmp(argv[1], "on") == 0) {
        g_debug_enabled = true;
        printf("Debug output enabled\n");
    } else if (strcmp(argv[1], "off") == 0) {
        g_debug_enabled = false;
        printf("Debug output disabled\n");
    } else {
        printf("Usage: debug on|off\n");
    }
}

/**
 * Reboot device
 */
static void cmd_reboot(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "bl") == 0) {
        printf("Rebooting into USB bootloader...\n");
        sleep_ms(100);
        reset_usb_boot(0, 0);
    } else {
        printf("Rebooting...\n");
        sleep_ms(100);
        watchdog_reboot(0, 0, 0);
    }
}

/**
 * Set WiFi credentials
 */
static void cmd_wifi(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: wifi <SSID> <PASSWORD>\n");
        printf("  SSID must not contain spaces\n");
        printf("  PASSWORD must not contain spaces\n");
        printf("  Credentials are saved for auto-connect on reboot\n");
        return;
    }

    const char *ssid = argv[1];
    const char *password = argv[2];

    if (strlen(ssid) > 32) {
        printf("Error: SSID too long (max 32 characters)\n");
        return;
    }

    if (strlen(password) > 64) {
        printf("Error: Password too long (max 64 characters)\n");
        return;
    }

    printf("Connecting to '%s'...\n", ssid);

    /* Extend watchdog timeout during blocking wifi connect (30s timeout) */
    watchdog_enable(35000, 1);

    if (wifi_connect(ssid, password)) {
        /* Restore normal watchdog timeout */
        watchdog_enable(8000, 1);
        printf("Connected successfully!\n");

        char ip_str[16];
        get_ip_address_str(ip_str, sizeof(ip_str));
        printf("IP Address: %s\n", ip_str);

        /* Save credentials for auto-connect */
        config_set_wifi(ssid, password, true);
        config_save();
        printf("Credentials saved for auto-connect\n");

        /* Start network services */
        ntp_server_init();
        ptp_server_init();
        web_init();
        printf("Network services started\n");
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
        printf("Usage: config <show|save|reset>\n");
        return;
    }

    if (strcmp(argv[1], "show") == 0) {
        config_print();
    } else if (strcmp(argv[1], "save") == 0) {
        config_save();
        printf("Configuration saved to flash\n");
    } else if (strcmp(argv[1], "reset") == 0) {
        config_reset();
        printf("Configuration reset to defaults\n");
        printf("Use 'config save' to persist, or 'reboot' to discard\n");
    } else {
        printf("Unknown config command: %s\n", argv[1]);
        printf("Valid commands: show, save, reset\n");
    }
}

/**
 * Configure pulse outputs
 */
static void cmd_pulse(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: pulse <pin> <mode> <params...>\n");
        printf("       pulse list | clear\n");
        printf("Type 'help' for full syntax\n");
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
        printf("Error: Invalid GPIO pin (0-28)\n");
        return;
    }

    if (argc < 3) {
        printf("Error: Missing mode (P/S/M/H/off)\n");
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
                printf("Usage: pulse <pin> P <interval_sec> <width_ms>\n");
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
                printf("Usage: pulse <pin> S <second> <width_ms> <count> <gap_ms>\n");
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
                printf("Usage: pulse <pin> M <minute> <width_ms> <count> <gap_ms>\n");
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
                printf("Usage: pulse <pin> H <HH:MM> <width_ms> <count> <gap_ms>\n");
                return;
            }
            uint8_t hour, minute;
            if (!parse_time(argv[3], &hour, &minute)) {
                printf("Error: Invalid time format (use HH:MM)\n");
                return;
            }
            uint16_t width = (uint16_t)atoi(argv[4]);
            uint16_t count = (uint16_t)atoi(argv[5]);
            uint16_t gap = (uint16_t)atoi(argv[6]);
            pulse_output_set_time((uint8_t)pin, hour, minute, width, count, gap);
            break;
        }

        default:
            printf("Error: Unknown mode '%c'\n", mode);
            printf("Valid modes: P (interval), S (second), M (minute), H (time), off\n");
            break;
    }
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
    } else {
        printf("Unknown command: %s\n", argv[0]);
        printf("Type 'help' for available commands\n");
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
