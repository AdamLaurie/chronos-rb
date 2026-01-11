/**
 * CHRONOS-Rb GNSS Input Module
 *
 * Handles GNSS receiver input (u-blox NEO-M8N, M9N, or similar) for backup timing.
 * Parses NMEA sentences: GPRMC, GPGGA, GPZDA, GPGSA (and GN* variants)
 * Captures PPS on GP11 for precise timing.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "chronos_rb.h"
#include "gnss_input.h"
#include "ac_freq_monitor.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define GNSS_UART           uart1
#define GNSS_UART_IRQ       UART1_IRQ
#define NMEA_BUFFER_SIZE    128
#define NMEA_FIELD_MAX      20

/* Current GPS-UTC leap second offset (as of Jan 1, 2017) */
#define GNSS_LEAP_SECONDS   18

/*============================================================================
 * UBX PROTOCOL DEFINITIONS
 *============================================================================*/

#define UBX_SYNC1           0xB5
#define UBX_SYNC2           0x62

/* UBX Message Classes */
#define UBX_CLASS_NAV       0x01
#define UBX_CLASS_CFG       0x06
#define UBX_CLASS_MGA       0x13

/* UBX Message IDs */
#define UBX_MGA_INI_TIME_UTC    0x40
#define UBX_CFG_NAV5            0x24
#define UBX_CFG_NAVX5           0x23
#define UBX_NAV_TIMEUTC         0x21
#define UBX_MON_VER             0x04
#define UBX_NAV_TIMELS          0x26

/* UBX Message Classes */
#define UBX_CLASS_MON       0x0A

/* GNSS module firmware info (used by UBX parser, declared early) */
static char gnss_fw_version[32] = "Unknown";
static char gnss_hw_version[16] = "Unknown";

/* Leap second info from UBX-NAV-TIMELS */
static int8_t gnss_leap_seconds = 0;
static bool gnss_leap_seconds_valid = false;

/*============================================================================
 * UBX PROTOCOL HELPERS
 *============================================================================*/

/**
 * Send UBX message
 */
static void ubx_send(uint8_t msg_class, uint8_t msg_id, const uint8_t *payload, uint16_t len) {
    uint8_t header[4] = {msg_class, msg_id, len & 0xFF, (len >> 8) & 0xFF};
    uint8_t ck_a, ck_b;

    /* Calculate checksum over class, id, length, and payload */
    ck_a = 0;
    ck_b = 0;
    for (int i = 0; i < 4; i++) {
        ck_a += header[i];
        ck_b += ck_a;
    }
    for (uint16_t i = 0; i < len; i++) {
        ck_a += payload[i];
        ck_b += ck_a;
    }

    /* Send sync bytes */
    uart_putc_raw(GNSS_UART, UBX_SYNC1);
    uart_putc_raw(GNSS_UART, UBX_SYNC2);

    /* Send header */
    for (int i = 0; i < 4; i++) {
        uart_putc_raw(GNSS_UART, header[i]);
    }

    /* Send payload */
    for (uint16_t i = 0; i < len; i++) {
        uart_putc_raw(GNSS_UART, payload[i]);
    }

    /* Send checksum */
    uart_putc_raw(GNSS_UART, ck_a);
    uart_putc_raw(GNSS_UART, ck_b);
}

/**
 * Send UBX-MGA-INI-TIME_UTC to provide leap second info
 * This tells the GNSS module the current GPS-UTC offset so it doesn't
 * have to wait 12.5 minutes for satellite almanac data
 */
static void ubx_send_leap_seconds(void) {
    /* UBX-MGA-INI-TIME_UTC payload (24 bytes) */
    uint8_t payload[24] = {0};

    payload[0] = 0x10;              /* type: UTC time */
    payload[1] = 0x00;              /* version */
    payload[2] = 0x00;              /* ref: none (just providing leap seconds) */
    payload[3] = GNSS_LEAP_SECONDS; /* leapSecs: current GPS-UTC offset */
    /* bytes 4-11: time fields (leave as 0 - not providing time) */
    /* bytes 12-15: ns (0) */
    payload[16] = 0xFF;             /* tAccS low: unknown accuracy */
    payload[17] = 0xFF;             /* tAccS high */
    /* bytes 18-19: reserved */
    payload[20] = 0xFF;             /* tAccNs low: unknown accuracy */
    payload[21] = 0xFF;
    payload[22] = 0xFF;
    payload[23] = 0xFF;             /* tAccNs high */

    ubx_send(UBX_CLASS_MGA, UBX_MGA_INI_TIME_UTC, payload, sizeof(payload));
    printf("[GNSS] Sent UBX-MGA-INI-TIME_UTC with leap seconds = %d\n", GNSS_LEAP_SECONDS);
}

/**
 * Request firmware version (UBX-MON-VER)
 */
static void ubx_request_version(void) {
    /* Poll message - empty payload */
    ubx_send(UBX_CLASS_MON, UBX_MON_VER, NULL, 0);
}

/**
 * Request leap second info (UBX-NAV-TIMELS)
 */
static void ubx_request_timels(void) {
    /* Poll message - empty payload */
    ubx_send(UBX_CLASS_NAV, UBX_NAV_TIMELS, NULL, 0);
}

/**
 * Buffer for UBX response parsing
 */
static uint8_t ubx_rx_buffer[256];
static uint16_t ubx_rx_idx = 0;
static uint8_t ubx_rx_state = 0;
static uint16_t ubx_rx_len = 0;
static uint8_t ubx_rx_class = 0;
static uint8_t ubx_rx_id = 0;

/**
 * Process received UBX byte (called from task, not IRQ)
 */
static void ubx_process_byte(uint8_t c) {
    switch (ubx_rx_state) {
        case 0: /* Waiting for sync1 */
            if (c == UBX_SYNC1) ubx_rx_state = 1;
            break;
        case 1: /* Waiting for sync2 */
            if (c == UBX_SYNC2) ubx_rx_state = 2;
            else ubx_rx_state = 0;
            break;
        case 2: /* Class */
            ubx_rx_class = c;
            ubx_rx_state = 3;
            break;
        case 3: /* ID */
            ubx_rx_id = c;
            ubx_rx_state = 4;
            break;
        case 4: /* Length low */
            ubx_rx_len = c;
            ubx_rx_state = 5;
            break;
        case 5: /* Length high */
            ubx_rx_len |= (c << 8);
            ubx_rx_idx = 0;
            ubx_rx_state = (ubx_rx_len > 0) ? 6 : 7;
            break;
        case 6: /* Payload */
            if (ubx_rx_idx < sizeof(ubx_rx_buffer)) {
                ubx_rx_buffer[ubx_rx_idx++] = c;
            }
            if (ubx_rx_idx >= ubx_rx_len) ubx_rx_state = 7;
            break;
        case 7: /* Checksum A (ignore for now) */
            ubx_rx_state = 8;
            break;
        case 8: /* Checksum B - message complete */
            /* Process UBX-MON-VER response */
            if (ubx_rx_class == UBX_CLASS_MON && ubx_rx_id == UBX_MON_VER && ubx_rx_len >= 40) {
                /* First 30 bytes: SW version string */
                /* Next 10 bytes: HW version string */
                memset(gnss_fw_version, 0, sizeof(gnss_fw_version));
                memset(gnss_hw_version, 0, sizeof(gnss_hw_version));
                memcpy(gnss_fw_version, ubx_rx_buffer, 30);
                memcpy(gnss_hw_version, ubx_rx_buffer + 30, 10);
                /* Trim trailing spaces/nulls */
                for (int i = 29; i >= 0 && (gnss_fw_version[i] == ' ' || gnss_fw_version[i] == '\0'); i--) {
                    gnss_fw_version[i] = '\0';
                }
                for (int i = 9; i >= 0 && (gnss_hw_version[i] == ' ' || gnss_hw_version[i] == '\0'); i--) {
                    gnss_hw_version[i] = '\0';
                }
                printf("[GNSS] Firmware: %s\n", gnss_fw_version);
                printf("[GNSS] Hardware: %s\n", gnss_hw_version);
            }
            /* Process UBX-NAV-TIMELS response */
            if (ubx_rx_class == UBX_CLASS_NAV && ubx_rx_id == UBX_NAV_TIMELS && ubx_rx_len >= 24) {
                /* Offset 8: srcOfCurrLs (0=default, 1=GPS, 2=SBAS, etc.) */
                /* Offset 9: currLs - current leap seconds */
                /* Offset 23: valid flags (bit 0 = validCurrLs) */
                uint8_t src = ubx_rx_buffer[8];
                int8_t curr_ls = (int8_t)ubx_rx_buffer[9];
                uint8_t valid = ubx_rx_buffer[23];
                gnss_leap_seconds = curr_ls;
                gnss_leap_seconds_valid = (valid & 0x01) != 0;
                const char *src_str = "unknown";
                if (src == 0) src_str = "default";
                else if (src == 1) src_str = "GPS";
                else if (src == 2) src_str = "SBAS";
                else if (src == 3) src_str = "BeiDou";
                else if (src == 4) src_str = "Galileo";
                else if (src == 5) src_str = "GLONASS";
                else if (src == 255) src_str = "none";
                printf("[GNSS] Leap seconds: %d (source: %s, valid: %s)\n",
                       curr_ls, src_str, gnss_leap_seconds_valid ? "yes" : "no");
            }
            ubx_rx_state = 0;
            break;
    }
}

/*============================================================================
 * STATE
 *============================================================================*/

static gnss_state_t gnss_state;
static volatile bool gnss_enabled = true;
static volatile bool gnss_debug = false;

/* PPS state - volatile for IRQ access */
static volatile uint64_t gnss_pps_timestamp = 0;
static volatile uint32_t gnss_pps_count = 0;
static volatile bool gnss_pps_triggered = false;

/* NMEA receive buffer */
static char nmea_buffer[NMEA_BUFFER_SIZE];
static uint8_t nmea_idx = 0;
static bool nmea_receiving = false;
static bool nmea_overflow = false;

/*============================================================================
 * NMEA PARSING HELPERS
 *============================================================================*/

/**
 * Calculate NMEA checksum
 */
static uint8_t nmea_checksum(const char *sentence) {
    uint8_t checksum = 0;
    /* Skip leading '$' if present */
    if (*sentence == '$') sentence++;
    /* XOR all characters until '*' or end */
    while (*sentence && *sentence != '*') {
        checksum ^= *sentence++;
    }
    return checksum;
}

/**
 * Verify NMEA checksum
 */
static bool nmea_verify_checksum(const char *sentence) {
    const char *star = strchr(sentence, '*');
    if (!star || strlen(star) < 3) return false;

    uint8_t calc = nmea_checksum(sentence);
    uint8_t recv = (uint8_t)strtol(star + 1, NULL, 16);

    return calc == recv;
}

/**
 * Get field from NMEA sentence (0-indexed)
 */
static bool nmea_get_field(const char *sentence, int field, char *buf, size_t buf_len) {
    const char *p = sentence;
    int current_field = 0;

    /* Skip to start of fields (after sentence type) */
    p = strchr(sentence, ',');
    if (!p) return false;

    /* Navigate to requested field */
    while (current_field < field) {
        p = strchr(p + 1, ',');
        if (!p) return false;
        current_field++;
    }
    p++;  /* Skip the comma */

    /* Copy field until next comma or asterisk */
    size_t i = 0;
    while (*p && *p != ',' && *p != '*' && i < buf_len - 1) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';

    return i > 0;
}

/**
 * Parse latitude from NMEA format (ddmm.mmmm)
 */
static double parse_latitude(const char *lat_str, const char *ns) {
    size_t len = strlen(lat_str);
    if (len < 4 || !ns || ns[0] == '\0') return 0.0;

    /* Validate digits */
    if (lat_str[0] < '0' || lat_str[0] > '9' ||
        lat_str[1] < '0' || lat_str[1] > '9') return 0.0;

    double deg = (lat_str[0] - '0') * 10 + (lat_str[1] - '0');
    double min = atof(lat_str + 2);

    /* Validate range */
    if (deg > 90 || min >= 60) return 0.0;

    double result = deg + min / 60.0;
    if (ns[0] == 'S' || ns[0] == 's') result = -result;
    return result;
}

/**
 * Parse longitude from NMEA format (dddmm.mmmm)
 */
static double parse_longitude(const char *lon_str, const char *ew) {
    size_t len = strlen(lon_str);
    if (len < 5 || !ew || ew[0] == '\0') return 0.0;

    /* Validate digits */
    if (lon_str[0] < '0' || lon_str[0] > '9' ||
        lon_str[1] < '0' || lon_str[1] > '9' ||
        lon_str[2] < '0' || lon_str[2] > '9') return 0.0;

    double deg = (lon_str[0] - '0') * 100 + (lon_str[1] - '0') * 10 + (lon_str[2] - '0');
    double min = atof(lon_str + 3);

    /* Validate range */
    if (deg > 180 || min >= 60) return 0.0;

    double result = deg + min / 60.0;
    if (ew[0] == 'W' || ew[0] == 'w') result = -result;
    return result;
}

/**
 * Convert GNSS time to Unix timestamp
 */
static uint32_t gnss_time_to_unix(const gnss_time_t *t) {
    if (!t->valid || t->year < 2000) return 0;

    /* Days from 1970 to year */
    uint32_t days = 0;
    for (int y = 1970; y < t->year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    /* Days in current year */
    static const int month_days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 1; m < t->month; m++) {
        days += month_days[m];
        if (m == 2 && (t->year % 4 == 0 && (t->year % 100 != 0 || t->year % 400 == 0))) {
            days++;  /* Leap year */
        }
    }
    days += t->day - 1;

    return days * 86400 + t->hour * 3600 + t->minute * 60 + t->second;
}

/*============================================================================
 * NMEA SENTENCE PARSERS
 *============================================================================*/

/**
 * Parse GPRMC/GNRMC - Recommended Minimum (time, date, position, speed, course)
 * $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh
 */
static void parse_gprmc(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 0: Time (hhmmss.ss) */
    if (nmea_get_field(sentence, 0, field, sizeof(field)) && strlen(field) >= 6) {
        gnss_state.time.hour = (field[0] - '0') * 10 + (field[1] - '0');
        gnss_state.time.minute = (field[2] - '0') * 10 + (field[3] - '0');
        gnss_state.time.second = (field[4] - '0') * 10 + (field[5] - '0');
        if (strlen(field) > 6) {
            gnss_state.time.millisecond = (uint16_t)(atof(field + 6) * 1000);
        }
    }

    /* Field 1: Status (A=active, V=void) */
    if (nmea_get_field(sentence, 1, field, sizeof(field))) {
        gnss_state.time.valid = (field[0] == 'A');
        gnss_state.position.valid = (field[0] == 'A');
    }

    /* Fields 2-5: Position */
    char lat[NMEA_FIELD_MAX], ns[NMEA_FIELD_MAX];
    char lon[NMEA_FIELD_MAX], ew[NMEA_FIELD_MAX];
    if (nmea_get_field(sentence, 2, lat, sizeof(lat)) &&
        nmea_get_field(sentence, 3, ns, sizeof(ns)) &&
        nmea_get_field(sentence, 4, lon, sizeof(lon)) &&
        nmea_get_field(sentence, 5, ew, sizeof(ew))) {
        gnss_state.position.latitude = parse_latitude(lat, ns);
        gnss_state.position.longitude = parse_longitude(lon, ew);
    }

    /* Field 6: Speed (knots) */
    if (nmea_get_field(sentence, 6, field, sizeof(field)) && strlen(field) > 0) {
        gnss_state.position.speed_knots = atof(field);
    }

    /* Field 7: Course */
    if (nmea_get_field(sentence, 7, field, sizeof(field)) && strlen(field) > 0) {
        gnss_state.position.course = atof(field);
    }

    /* Field 8: Date (ddmmyy) */
    if (nmea_get_field(sentence, 8, field, sizeof(field)) && strlen(field) >= 6) {
        gnss_state.time.day = (field[0] - '0') * 10 + (field[1] - '0');
        gnss_state.time.month = (field[2] - '0') * 10 + (field[3] - '0');
        int year = (field[4] - '0') * 10 + (field[5] - '0');
        gnss_state.time.year = (year < 80) ? 2000 + year : 1900 + year;
    }

    if (gnss_debug && gnss_state.time.valid) {
        printf("[GNSS] RMC: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
               gnss_state.time.year, gnss_state.time.month, gnss_state.time.day,
               gnss_state.time.hour, gnss_state.time.minute, gnss_state.time.second);
    }
}

/**
 * Parse GPGGA/GNGGA - Fix data (position, altitude, satellites, HDOP)
 * $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
 */
static void parse_gpgga(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 5: Fix quality (0=none, 1=GPS, 2=DGPS) */
    if (nmea_get_field(sentence, 5, field, sizeof(field))) {
        int quality = atoi(field);
        if (quality == 0) {
            gnss_state.fix_type = GNSS_FIX_NONE;
        } else {
            gnss_state.fix_type = GNSS_FIX_3D;  /* Assume 3D if any fix */
        }
    }

    /* Field 6: Number of satellites */
    if (nmea_get_field(sentence, 6, field, sizeof(field))) {
        gnss_state.satellites = atoi(field);
    }

    /* Field 7: HDOP */
    if (nmea_get_field(sentence, 7, field, sizeof(field)) && strlen(field) > 0) {
        gnss_state.position.hdop = atof(field);
    }

    /* Field 8: Altitude */
    if (nmea_get_field(sentence, 8, field, sizeof(field)) && strlen(field) > 0) {
        gnss_state.position.altitude = atof(field);
    }
}

/**
 * Parse GPGSA/GNGSA - DOP and active satellites
 * $GPGSA,A,3,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,x.x,x.x,x.x*hh
 */
static void parse_gpgsa(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 1: Fix type (1=none, 2=2D, 3=3D) */
    if (nmea_get_field(sentence, 1, field, sizeof(field))) {
        int fix = atoi(field);
        switch (fix) {
            case 2: gnss_state.fix_type = GNSS_FIX_2D; break;
            case 3: gnss_state.fix_type = GNSS_FIX_3D; break;
            default: gnss_state.fix_type = GNSS_FIX_NONE; break;
        }
    }
}

/**
 * Parse GPZDA/GNZDA - Date & Time
 * $GPZDA,hhmmss.ss,dd,mm,yyyy,xx,xx*hh
 */
static void parse_gpzda(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 0: Time */
    if (nmea_get_field(sentence, 0, field, sizeof(field)) && strlen(field) >= 6) {
        gnss_state.time.hour = (field[0] - '0') * 10 + (field[1] - '0');
        gnss_state.time.minute = (field[2] - '0') * 10 + (field[3] - '0');
        gnss_state.time.second = (field[4] - '0') * 10 + (field[5] - '0');
    }

    /* Field 1: Day */
    if (nmea_get_field(sentence, 1, field, sizeof(field))) {
        gnss_state.time.day = atoi(field);
    }

    /* Field 2: Month */
    if (nmea_get_field(sentence, 2, field, sizeof(field))) {
        gnss_state.time.month = atoi(field);
    }

    /* Field 3: Year */
    if (nmea_get_field(sentence, 3, field, sizeof(field))) {
        gnss_state.time.year = atoi(field);
        if (gnss_state.time.year > 0) {
            gnss_state.time.valid = true;
        }
    }
}

/**
 * Process complete NMEA sentence
 */
static void process_nmea_sentence(const char *sentence) {
    if (!nmea_verify_checksum(sentence)) {
        gnss_state.nmea_errors++;
        return;
    }

    gnss_state.nmea_count++;
    gnss_state.last_nmea_us = time_us_64();

    /* Route to appropriate parser */
    if (strncmp(sentence, "$GPRMC", 6) == 0 || strncmp(sentence, "$GNRMC", 6) == 0) {
        parse_gprmc(sentence);
    } else if (strncmp(sentence, "$GPGGA", 6) == 0 || strncmp(sentence, "$GNGGA", 6) == 0) {
        parse_gpgga(sentence);
    } else if (strncmp(sentence, "$GPGSA", 6) == 0 || strncmp(sentence, "$GNGSA", 6) == 0) {
        parse_gpgsa(sentence);
    } else if (strncmp(sentence, "$GPZDA", 6) == 0 || strncmp(sentence, "$GNZDA", 6) == 0) {
        parse_gpzda(sentence);
    }
}

/*============================================================================
 * PPS IRQ HANDLER - GPIO callback for GNSS PPS
 *============================================================================*/

/**
 * Shared GPIO interrupt callback for all GPIO IRQs
 *
 * Handles:
 *   - GNSS PPS (GP11) - rising edge
 *   - AC zero crossing (GP19) - falling edge
 *
 * Note: Rubidium PPS uses PIO interrupts (not GPIO), so no conflict.
 * The Pico only allows one GPIO callback per core, so all GPIO IRQs
 * must be dispatched from this single handler.
 */
static void shared_gpio_callback(uint gpio, uint32_t events) {
    if (gpio == GPIO_GNSS_PPS_INPUT && (events & GPIO_IRQ_EDGE_RISE)) {
        /* GNSS PPS - capture 10MHz counter first for accurate offset */
        freq_counter_capture_gnss_pps();
        gnss_pps_timestamp = time_us_64();
        gnss_pps_count++;
        gnss_pps_triggered = true;
    }
    if (gpio == GPIO_AC_ZERO_CROSS && (events & GPIO_IRQ_EDGE_FALL)) {
        /* AC mains zero crossing */
        ac_zero_cross_irq_handler();
    }
}

/*============================================================================
 * UART RECEIVE HANDLER
 *============================================================================*/

static void gnss_uart_handler(void) {
    while (uart_is_readable(GNSS_UART)) {
        char c = uart_getc(GNSS_UART);

        /* Process UBX binary protocol (starts with 0xB5 0x62) */
        ubx_process_byte((uint8_t)c);

        if (c == '$') {
            /* Start of new NMEA sentence - reset state */
            nmea_idx = 0;
            nmea_receiving = true;
            nmea_overflow = false;
        }

        if (nmea_receiving) {
            if (nmea_idx < NMEA_BUFFER_SIZE - 1) {
                nmea_buffer[nmea_idx++] = c;
            } else {
                /* Buffer overflow - mark and discard rest of sentence */
                nmea_overflow = true;
            }

            if (c == '\n' || c == '\r') {
                /* End of sentence */
                nmea_buffer[nmea_idx] = '\0';
                nmea_receiving = false;

                /* Only process if no overflow and valid length */
                if (!nmea_overflow && nmea_idx > 10) {
                    process_nmea_sentence(nmea_buffer);
                } else if (nmea_overflow) {
                    gnss_state.nmea_errors++;
                }
            }
        }
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Flush UART receive buffer
 */
static void gnss_flush_uart(void) {
    while (uart_is_readable(GNSS_UART)) {
        uart_getc(GNSS_UART);
    }
    nmea_idx = 0;
    nmea_receiving = false;
}

/**
 * Initialize GNSS input
 */
void gnss_input_init(void) {
    printf("[GNSS] Initializing GNSS receiver input\n");

    /* Clear state */
    memset(&gnss_state, 0, sizeof(gnss_state));
    gnss_enabled = true;
    gnss_pps_timestamp = 0;
    gnss_pps_count = 0;
    gnss_pps_triggered = false;

    /* Initialize UART1 for GNSS */
    uart_init(GNSS_UART, GPS_UART_BAUD);
    gpio_set_function(GPIO_GNSS_RX, GPIO_FUNC_UART);
    gpio_set_function(GPIO_GNSS_TX, GPIO_FUNC_UART);  /* TX for sending commands to GNSS */

    /* Configure UART */
    uart_set_hw_flow(GNSS_UART, false, false);
    uart_set_format(GNSS_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(GNSS_UART, true);

    /* Flush any stale data in UART buffer before enabling interrupts */
    gnss_flush_uart();

    /* Set up UART interrupt */
    irq_set_exclusive_handler(GNSS_UART_IRQ, gnss_uart_handler);
    irq_set_enabled(GNSS_UART_IRQ, true);
    uart_set_irq_enables(GNSS_UART, true, false);

    /* Initialize GNSS PPS input GPIO */
    gpio_init(GPIO_GNSS_PPS_INPUT);
    gpio_set_dir(GPIO_GNSS_PPS_INPUT, GPIO_IN);
    gpio_pull_down(GPIO_GNSS_PPS_INPUT);

    /* Enable GPIO IRQ for GNSS PPS with shared callback
     * This callback also handles AC zero crossing (GP19)
     * Note: Rubidium PPS uses PIO interrupts (not GPIO), so no conflict
     */
    gpio_set_irq_enabled_with_callback(GPIO_GNSS_PPS_INPUT, GPIO_IRQ_EDGE_RISE, true,
                                       shared_gpio_callback);

    printf("[GNSS] UART1: GP%d (RX from GNSS), GP%d (TX to GNSS)\n", GPIO_GNSS_RX, GPIO_GNSS_TX);
    printf("[GNSS] PPS: GP%d (GPIO IRQ callback)\n", GPIO_GNSS_PPS_INPUT);

    /* Give GNSS module time to start up, then configure it */
    sleep_ms(500);

    /* Request firmware version */
    printf("[GNSS] Requesting GNSS module info...\n");
    ubx_request_version();
    sleep_ms(100);

    /* Query current leap second status */
    ubx_request_timels();
    sleep_ms(100);

    /* Send leap second configuration (may not work on all modules) */
    ubx_send_leap_seconds();

    printf("[GNSS] Waiting for GNSS fix...\n");
}

/**
 * GNSS task - call from main loop
 * Processes PPS events from IRQ and checks timeouts
 */
void gnss_input_task(void) {
    static uint64_t last_leap_query_us = 0;

    if (!gnss_enabled) return;

    uint64_t now = time_us_64();

    /* Process PPS from IRQ handler (atomic read) */
    if (gnss_pps_triggered) {
        uint32_t irq = save_and_disable_interrupts();
        gnss_state.last_pps_us = gnss_pps_timestamp;
        gnss_state.pps_count = gnss_pps_count;
        gnss_state.pps_valid = true;
        gnss_pps_triggered = false;
        restore_interrupts(irq);
    }

    /* Check PPS timeout */
    if (gnss_state.pps_valid && (now - gnss_state.last_pps_us) > GPS_PPS_TIMEOUT_MS * 1000) {
        gnss_state.pps_valid = false;
    }

    /* Check NMEA timeout */
    if (gnss_state.time.valid && (now - gnss_state.last_nmea_us) > GPS_NMEA_TIMEOUT_MS * 1000) {
        gnss_state.time.valid = false;
        gnss_state.position.valid = false;
        gnss_state.fix_type = GNSS_FIX_NONE;
    }

    /* Periodically re-query leap second status (every 60 seconds) */
    if (!gnss_leap_seconds_valid && (now - last_leap_query_us) > 60000000ULL) {
        ubx_request_timels();
        last_leap_query_us = now;
    }

    /* Re-query firmware version if still unknown (every 10 seconds for first 2 minutes) */
    static uint64_t last_ver_query_us = 0;
    static int ver_query_count = 0;
    if (strcmp(gnss_fw_version, "Unknown") == 0 && ver_query_count < 12) {
        if ((now - last_ver_query_us) > 10000000ULL) {  /* 10 seconds */
            ubx_request_version();
            last_ver_query_us = now;
            ver_query_count++;
        }
    }
}

/**
 * Check if GNSS has a valid fix
 */
bool gnss_has_fix(void) {
    return gnss_state.fix_type != GNSS_FIX_NONE && gnss_state.satellites >= GPS_MIN_SATS;
}

/**
 * Check if GNSS has valid time
 */
bool gnss_has_time(void) {
    return gnss_state.time.valid && gnss_state.time.year >= 2020;
}

/**
 * Check if GNSS PPS is valid
 */
bool gnss_pps_valid(void) {
    return gnss_state.pps_valid;
}

/**
 * Get number of satellites in use
 */
uint8_t gnss_get_satellites(void) {
    return gnss_state.satellites;
}

/**
 * Get fix type
 */
gnss_fix_type_t gnss_get_fix_type(void) {
    return gnss_state.fix_type;
}

/**
 * Get Unix timestamp from GNSS
 */
uint32_t gnss_get_unix_time(void) {
    if (!gnss_state.time.valid) return 0;
    return gnss_time_to_unix(&gnss_state.time);
}

/**
 * Get timestamp of last NMEA time update
 */
uint64_t gnss_get_last_nmea_us(void) {
    return gnss_state.last_nmea_us;
}

/**
 * Get UTC time structure
 */
void gnss_get_utc_time(gnss_time_t *time) {
    if (time) {
        *time = gnss_state.time;
    }
}

/**
 * Get last PPS timestamp
 */
uint64_t gnss_get_last_pps_us(void) {
    return gnss_state.last_pps_us;
}

/**
 * Get PPS count
 */
uint32_t gnss_get_pps_count(void) {
    return gnss_state.pps_count;
}

/**
 * Get position (simple interface)
 */
void gnss_get_position(double *lat, double *lon, double *alt) {
    if (lat) *lat = gnss_state.position.latitude;
    if (lon) *lon = gnss_state.position.longitude;
    if (alt) *alt = gnss_state.position.altitude;
}

/**
 * Get full position structure
 */
void gnss_get_position_full(gnss_position_t *pos) {
    if (pos) {
        *pos = gnss_state.position;
    }
}

/**
 * Get complete GNSS state
 */
const gnss_state_t* gnss_get_state(void) {
    return &gnss_state;
}

/**
 * Enable/disable GNSS
 */
void gnss_enable(bool enable) {
    uint32_t irq = save_and_disable_interrupts();
    gnss_enabled = enable;
    restore_interrupts(irq);

    if (!enable) {
        /* Disable UART RX interrupt */
        uart_set_irq_enables(GNSS_UART, false, false);
        /* Disable PPS GPIO interrupt */
        gpio_set_irq_enabled(GPIO_GNSS_PPS_INPUT, GPIO_IRQ_EDGE_RISE, false);
    } else {
        /* Enable UART RX interrupt */
        uart_set_irq_enables(GNSS_UART, true, false);
        /* Enable PPS GPIO interrupt */
        gpio_set_irq_enabled(GPIO_GNSS_PPS_INPUT, GPIO_IRQ_EDGE_RISE, true);
    }
    printf("[GNSS] GNSS input %s\n", enable ? "enabled" : "disabled");
}

/**
 * Check if GNSS is enabled
 */
bool gnss_is_enabled(void) {
    return gnss_enabled;
}

/**
 * Reset GNSS time state for resync
 * Flushes UART and clears time validity so fresh data will be used
 */
void gnss_reset_time(void) {
    uint32_t irq = save_and_disable_interrupts();
    gnss_flush_uart();
    gnss_state.time.valid = false;
    gnss_state.last_nmea_us = 0;
    restore_interrupts(irq);
    printf("[GNSS] Time state reset, waiting for fresh NMEA\n");
}

/**
 * Enable/disable GNSS debug output
 */
void gnss_set_debug(bool enable) {
    gnss_debug = enable;
    printf("[GNSS] Debug %s\n", enable ? "enabled" : "disabled");
}

/**
 * Check if GNSS debug is enabled
 */
bool gnss_get_debug(void) {
    return gnss_debug;
}

/**
 * Get GNSS module firmware version string
 */
const char* gnss_get_firmware_version(void) {
    return gnss_fw_version;
}

/**
 * Get GNSS module hardware version string
 */
const char* gnss_get_hardware_version(void) {
    return gnss_hw_version;
}

/**
 * Get GNSS module leap second info
 */
int8_t gnss_get_leap_seconds(void) {
    return gnss_leap_seconds;
}

/**
 * Check if GNSS leap seconds are valid
 */
bool gnss_leap_seconds_is_valid(void) {
    return gnss_leap_seconds_valid;
}
