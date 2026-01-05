/**
 * CHRONOS-Rb GPS Input Module
 *
 * Handles GPS receiver input (NEO-M8N or similar) for backup timing.
 * Parses NMEA sentences: GPRMC, GPGGA, GPZDA, GPGSA
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
#include "gps_input.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define GPS_UART            uart1
#define GPS_UART_IRQ        UART1_IRQ
#define NMEA_BUFFER_SIZE    128
#define NMEA_FIELD_MAX      20

/*============================================================================
 * STATE
 *============================================================================*/

static gps_state_t gps_state;
static volatile bool gps_enabled = true;

/* PPS state - volatile for IRQ access */
static volatile uint64_t gps_pps_timestamp = 0;
static volatile uint32_t gps_pps_count = 0;
static volatile bool gps_pps_triggered = false;

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
 * Convert GPS time to Unix timestamp
 */
static uint32_t gps_time_to_unix(const gps_time_t *t) {
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
 * Parse GPRMC - Recommended Minimum (time, date, position, speed, course)
 * $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh
 */
static void parse_gprmc(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 0: Time (hhmmss.ss) */
    if (nmea_get_field(sentence, 0, field, sizeof(field)) && strlen(field) >= 6) {
        gps_state.time.hour = (field[0] - '0') * 10 + (field[1] - '0');
        gps_state.time.minute = (field[2] - '0') * 10 + (field[3] - '0');
        gps_state.time.second = (field[4] - '0') * 10 + (field[5] - '0');
        if (strlen(field) > 6) {
            gps_state.time.millisecond = (uint16_t)(atof(field + 6) * 1000);
        }
    }

    /* Field 1: Status (A=active, V=void) */
    if (nmea_get_field(sentence, 1, field, sizeof(field))) {
        gps_state.time.valid = (field[0] == 'A');
        gps_state.position.valid = (field[0] == 'A');
    }

    /* Fields 2-5: Position */
    char lat[NMEA_FIELD_MAX], ns[NMEA_FIELD_MAX];
    char lon[NMEA_FIELD_MAX], ew[NMEA_FIELD_MAX];
    if (nmea_get_field(sentence, 2, lat, sizeof(lat)) &&
        nmea_get_field(sentence, 3, ns, sizeof(ns)) &&
        nmea_get_field(sentence, 4, lon, sizeof(lon)) &&
        nmea_get_field(sentence, 5, ew, sizeof(ew))) {
        gps_state.position.latitude = parse_latitude(lat, ns);
        gps_state.position.longitude = parse_longitude(lon, ew);
    }

    /* Field 6: Speed (knots) */
    if (nmea_get_field(sentence, 6, field, sizeof(field)) && strlen(field) > 0) {
        gps_state.position.speed_knots = atof(field);
    }

    /* Field 7: Course */
    if (nmea_get_field(sentence, 7, field, sizeof(field)) && strlen(field) > 0) {
        gps_state.position.course = atof(field);
    }

    /* Field 8: Date (ddmmyy) */
    if (nmea_get_field(sentence, 8, field, sizeof(field)) && strlen(field) >= 6) {
        gps_state.time.day = (field[0] - '0') * 10 + (field[1] - '0');
        gps_state.time.month = (field[2] - '0') * 10 + (field[3] - '0');
        int year = (field[4] - '0') * 10 + (field[5] - '0');
        gps_state.time.year = (year < 80) ? 2000 + year : 1900 + year;
    }
}

/**
 * Parse GPGGA - Fix data (position, altitude, satellites, HDOP)
 * $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
 */
static void parse_gpgga(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 5: Fix quality (0=none, 1=GPS, 2=DGPS) */
    if (nmea_get_field(sentence, 5, field, sizeof(field))) {
        int quality = atoi(field);
        if (quality == 0) {
            gps_state.fix_type = GPS_FIX_NONE;
        } else {
            gps_state.fix_type = GPS_FIX_3D;  /* Assume 3D if any fix */
        }
    }

    /* Field 6: Number of satellites */
    if (nmea_get_field(sentence, 6, field, sizeof(field))) {
        gps_state.satellites = atoi(field);
    }

    /* Field 7: HDOP */
    if (nmea_get_field(sentence, 7, field, sizeof(field)) && strlen(field) > 0) {
        gps_state.position.hdop = atof(field);
    }

    /* Field 8: Altitude */
    if (nmea_get_field(sentence, 8, field, sizeof(field)) && strlen(field) > 0) {
        gps_state.position.altitude = atof(field);
    }
}

/**
 * Parse GPGSA - DOP and active satellites
 * $GPGSA,A,3,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,x.x,x.x,x.x*hh
 */
static void parse_gpgsa(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 1: Fix type (1=none, 2=2D, 3=3D) */
    if (nmea_get_field(sentence, 1, field, sizeof(field))) {
        int fix = atoi(field);
        switch (fix) {
            case 2: gps_state.fix_type = GPS_FIX_2D; break;
            case 3: gps_state.fix_type = GPS_FIX_3D; break;
            default: gps_state.fix_type = GPS_FIX_NONE; break;
        }
    }
}

/**
 * Parse GPZDA - Date & Time
 * $GPZDA,hhmmss.ss,dd,mm,yyyy,xx,xx*hh
 */
static void parse_gpzda(const char *sentence) {
    char field[NMEA_FIELD_MAX];

    /* Field 0: Time */
    if (nmea_get_field(sentence, 0, field, sizeof(field)) && strlen(field) >= 6) {
        gps_state.time.hour = (field[0] - '0') * 10 + (field[1] - '0');
        gps_state.time.minute = (field[2] - '0') * 10 + (field[3] - '0');
        gps_state.time.second = (field[4] - '0') * 10 + (field[5] - '0');
    }

    /* Field 1: Day */
    if (nmea_get_field(sentence, 1, field, sizeof(field))) {
        gps_state.time.day = atoi(field);
    }

    /* Field 2: Month */
    if (nmea_get_field(sentence, 2, field, sizeof(field))) {
        gps_state.time.month = atoi(field);
    }

    /* Field 3: Year */
    if (nmea_get_field(sentence, 3, field, sizeof(field))) {
        gps_state.time.year = atoi(field);
        if (gps_state.time.year > 0) {
            gps_state.time.valid = true;
        }
    }
}

/**
 * Process complete NMEA sentence
 */
static void process_nmea_sentence(const char *sentence) {
    if (!nmea_verify_checksum(sentence)) {
        gps_state.nmea_errors++;
        return;
    }

    gps_state.nmea_count++;
    gps_state.last_nmea_us = time_us_64();

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
 * PPS IRQ HANDLER - GPIO callback for GPS PPS
 *============================================================================*/

/**
 * GPS PPS GPIO interrupt callback
 *
 * Note: This is separate from rubidium PPS which uses PIO interrupts.
 * The Pico allows one GPIO callback per core, but rubidium PPS uses
 * PIO (not GPIO callbacks), so there's no conflict.
 */
static void gps_pps_gpio_callback(uint gpio, uint32_t events) {
    if (gpio == GPIO_GPS_PPS_INPUT && (events & GPIO_IRQ_EDGE_RISE)) {
        gps_pps_timestamp = time_us_64();
        gps_pps_count++;
        gps_pps_triggered = true;
    }
}

/*============================================================================
 * UART RECEIVE HANDLER
 *============================================================================*/

static void gps_uart_handler(void) {
    while (uart_is_readable(GPS_UART)) {
        char c = uart_getc(GPS_UART);

        if (c == '$') {
            /* Start of new sentence - reset state */
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
                    gps_state.nmea_errors++;
                }
            }
        }
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize GPS input
 */
void gps_input_init(void) {
    printf("[GPS] Initializing GPS receiver input\n");

    /* Clear state */
    memset(&gps_state, 0, sizeof(gps_state));
    gps_enabled = true;
    gps_pps_timestamp = 0;
    gps_pps_count = 0;
    gps_pps_triggered = false;

    /* Initialize UART1 for GPS */
    uart_init(GPS_UART, GPS_UART_BAUD);
    gpio_set_function(GPIO_GPS_RX, GPIO_FUNC_UART);
    gpio_set_function(GPIO_NMEA_TX, GPIO_FUNC_UART);  /* TX for sending commands to GPS */

    /* Configure UART */
    uart_set_hw_flow(GPS_UART, false, false);
    uart_set_format(GPS_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(GPS_UART, true);

    /* Set up UART interrupt */
    irq_set_exclusive_handler(GPS_UART_IRQ, gps_uart_handler);
    irq_set_enabled(GPS_UART_IRQ, true);
    uart_set_irq_enables(GPS_UART, true, false);

    /* Initialize GPS PPS input GPIO */
    gpio_init(GPIO_GPS_PPS_INPUT);
    gpio_set_dir(GPIO_GPS_PPS_INPUT, GPIO_IN);
    gpio_pull_down(GPIO_GPS_PPS_INPUT);

    /* Enable GPIO IRQ for GPS PPS with callback
     * Note: Rubidium PPS uses PIO interrupts (not GPIO), so no conflict
     */
    gpio_set_irq_enabled_with_callback(GPIO_GPS_PPS_INPUT, GPIO_IRQ_EDGE_RISE, true,
                                       gps_pps_gpio_callback);

    printf("[GPS] UART1: GP%d (RX from GPS), GP%d (TX to GPS)\n", GPIO_GPS_RX, GPIO_NMEA_TX);
    printf("[GPS] PPS: GP%d (GPIO IRQ callback)\n", GPIO_GPS_PPS_INPUT);
    printf("[GPS] Waiting for GPS fix...\n");
}

/**
 * GPS task - call from main loop
 * Processes PPS events from IRQ and checks timeouts
 */
void gps_input_task(void) {
    if (!gps_enabled) return;

    uint64_t now = time_us_64();

    /* Process PPS from IRQ handler (atomic read) */
    if (gps_pps_triggered) {
        uint32_t irq = save_and_disable_interrupts();
        gps_state.last_pps_us = gps_pps_timestamp;
        gps_state.pps_count = gps_pps_count;
        gps_state.pps_valid = true;
        gps_pps_triggered = false;
        restore_interrupts(irq);
    }

    /* Check PPS timeout */
    if (gps_state.pps_valid && (now - gps_state.last_pps_us) > GPS_PPS_TIMEOUT_MS * 1000) {
        gps_state.pps_valid = false;
    }

    /* Check NMEA timeout */
    if (gps_state.time.valid && (now - gps_state.last_nmea_us) > GPS_NMEA_TIMEOUT_MS * 1000) {
        gps_state.time.valid = false;
        gps_state.position.valid = false;
        gps_state.fix_type = GPS_FIX_NONE;
    }
}

/**
 * Check if GPS has a valid fix
 */
bool gps_has_fix(void) {
    return gps_state.fix_type != GPS_FIX_NONE && gps_state.satellites >= GPS_MIN_SATS;
}

/**
 * Check if GPS has valid time
 */
bool gps_has_time(void) {
    return gps_state.time.valid && gps_state.time.year >= 2020;
}

/**
 * Check if GPS PPS is valid
 */
bool gps_pps_valid(void) {
    return gps_state.pps_valid;
}

/**
 * Get number of satellites in use
 */
uint8_t gps_get_satellites(void) {
    return gps_state.satellites;
}

/**
 * Get fix type
 */
gps_fix_type_t gps_get_fix_type(void) {
    return gps_state.fix_type;
}

/**
 * Get Unix timestamp from GPS
 */
uint32_t gps_get_unix_time(void) {
    if (!gps_state.time.valid) return 0;
    return gps_time_to_unix(&gps_state.time);
}

/**
 * Get UTC time structure
 */
void gps_get_utc_time(gps_time_t *time) {
    if (time) {
        *time = gps_state.time;
    }
}

/**
 * Get last PPS timestamp
 */
uint64_t gps_get_last_pps_us(void) {
    return gps_state.last_pps_us;
}

/**
 * Get PPS count
 */
uint32_t gps_get_pps_count(void) {
    return gps_state.pps_count;
}

/**
 * Get position (simple interface)
 */
void gps_get_position(double *lat, double *lon, double *alt) {
    if (lat) *lat = gps_state.position.latitude;
    if (lon) *lon = gps_state.position.longitude;
    if (alt) *alt = gps_state.position.altitude;
}

/**
 * Get full position structure
 */
void gps_get_position_full(gps_position_t *pos) {
    if (pos) {
        *pos = gps_state.position;
    }
}

/**
 * Get complete GPS state
 */
const gps_state_t* gps_get_state(void) {
    return &gps_state;
}

/**
 * Enable/disable GPS
 */
void gps_enable(bool enable) {
    uint32_t irq = save_and_disable_interrupts();
    gps_enabled = enable;
    restore_interrupts(irq);

    if (!enable) {
        /* Disable UART RX interrupt */
        uart_set_irq_enables(GPS_UART, false, false);
        /* Disable PPS GPIO interrupt */
        gpio_set_irq_enabled(GPIO_GPS_PPS_INPUT, GPIO_IRQ_EDGE_RISE, false);
    } else {
        /* Enable UART RX interrupt */
        uart_set_irq_enables(GPS_UART, true, false);
        /* Enable PPS GPIO interrupt */
        gpio_set_irq_enabled(GPIO_GPS_PPS_INPUT, GPIO_IRQ_EDGE_RISE, true);
    }
    printf("[GPS] GPS input %s\n", enable ? "enabled" : "disabled");
}

/**
 * Check if GPS is enabled
 */
bool gps_is_enabled(void) {
    return gps_enabled;
}
