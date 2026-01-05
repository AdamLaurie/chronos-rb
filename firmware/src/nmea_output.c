/**
 * CHRONOS-Rb NMEA 0183 Output
 *
 * Outputs GPS-compatible NMEA sentences over UART for devices expecting GPS time.
 * Synchronized to rubidium reference, outputs at 1Hz on PPS edge.
 *
 * Sentences:
 *   $GPZDA - Time & Date
 *   $GPRMC - Recommended Minimum (with position placeholder)
 *
 * UART: 4800 baud, 8N1 (standard GPS) on UART1
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "chronos_rb.h"
#include "nmea_output.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define NMEA_UART           uart1
#define NMEA_UART_TX        28      /* GP28 - UART1 TX */
#define NMEA_UART_RX        -1      /* Not used (output only) */
#define NMEA_BAUD_RATE      4800    /* Standard GPS baud rate */

/* NTP epoch offset */
#define NTP_UNIX_OFFSET     2208988800UL

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static bool nmea_initialized = false;
static bool nmea_enabled = true;
static uint32_t nmea_sentences_sent = 0;
static uint32_t last_pps_count = 0;

/*============================================================================
 * CHECKSUM CALCULATION
 *============================================================================*/

/**
 * Calculate NMEA checksum (XOR of all characters between $ and *)
 */
static uint8_t nmea_checksum(const char *sentence) {
    uint8_t cs = 0;
    /* Skip the leading '$' */
    const char *p = sentence + 1;
    while (*p && *p != '*') {
        cs ^= *p++;
    }
    return cs;
}

/*============================================================================
 * TIME CONVERSION
 *============================================================================*/

/**
 * Convert NTP timestamp to broken-down UTC time
 */
static void ntp_to_utc(uint32_t ntp_secs, int *year, int *month, int *day,
                       int *hour, int *min, int *sec) {
    /* Convert NTP to Unix time */
    uint32_t unix_time = ntp_secs - NTP_UNIX_OFFSET;

    /* Time of day */
    uint32_t days = unix_time / 86400;
    uint32_t remaining = unix_time % 86400;

    *hour = remaining / 3600;
    *min = (remaining % 3600) / 60;
    *sec = remaining % 60;

    /* Calculate year */
    int y = 1970;
    while (1) {
        int days_in_year = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (days < (uint32_t)days_in_year) break;
        days -= days_in_year;
        y++;
    }
    *year = y;

    /* Days in each month */
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) {
        days_in_month[1] = 29;
    }

    int m = 0;
    while (days >= (uint32_t)days_in_month[m]) {
        days -= days_in_month[m];
        m++;
    }
    *month = m + 1;  /* 1-based month */
    *day = days + 1;
}

/*============================================================================
 * NMEA SENTENCE GENERATION
 *============================================================================*/

/**
 * Send NMEA sentence over UART
 */
static void nmea_send(const char *sentence) {
    uart_puts(NMEA_UART, sentence);
    nmea_sentences_sent++;
}

/**
 * Generate and send $GPZDA sentence
 * Format: $GPZDA,hhmmss.ss,dd,mm,yyyy,xx,yy*cs
 *   hhmmss.ss = UTC time
 *   dd = day
 *   mm = month
 *   yyyy = year
 *   xx = local zone hours (00)
 *   yy = local zone minutes (00)
 */
static void nmea_send_gpzda(void) {
    timestamp_t ts = get_current_time();
    int year, month, day, hour, min, sec;
    ntp_to_utc(ts.seconds, &year, &month, &day, &hour, &min, &sec);

    /* Calculate fractional seconds from timestamp fraction */
    int centisec = (ts.fraction >> 24) * 100 / 256;

    char sentence[80];
    snprintf(sentence, sizeof(sentence),
             "$GPZDA,%02d%02d%02d.%02d,%02d,%02d,%04d,00,00",
             hour, min, sec, centisec, day, month, year);

    /* Append checksum */
    char full[84];
    snprintf(full, sizeof(full), "%s*%02X\r\n", sentence, nmea_checksum(sentence));
    nmea_send(full);
}

/**
 * Generate and send $GPRMC sentence (Recommended Minimum)
 * Format: $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*cs
 * We output a minimal valid sentence since we don't have actual GPS position
 */
static void nmea_send_gprmc(void) {
    timestamp_t ts = get_current_time();
    int year, month, day, hour, min, sec;
    ntp_to_utc(ts.seconds, &year, &month, &day, &hour, &min, &sec);

    int centisec = (ts.fraction >> 24) * 100 / 256;

    /* Status: A=valid, V=invalid. We say A if time is valid */
    char status = g_time_state.time_valid ? 'A' : 'V';

    char sentence[100];
    snprintf(sentence, sizeof(sentence),
             "$GPRMC,%02d%02d%02d.%02d,%c,0000.0000,N,00000.0000,W,0.0,0.0,%02d%02d%02d,0.0,E",
             hour, min, sec, centisec, status,
             day, month, year % 100);

    char full[104];
    snprintf(full, sizeof(full), "%s*%02X\r\n", sentence, nmea_checksum(sentence));
    nmea_send(full);
}

/**
 * Generate and send $GPGGA sentence (Fix data)
 * Minimal output indicating time-only (no position fix)
 */
static void nmea_send_gpgga(void) {
    timestamp_t ts = get_current_time();
    int year, month, day, hour, min, sec;
    ntp_to_utc(ts.seconds, &year, &month, &day, &hour, &min, &sec);
    (void)year; (void)month; (void)day;

    int centisec = (ts.fraction >> 24) * 100 / 256;

    /* Fix quality: 0=invalid, 1=GPS, 2=DGPS. Use 0 since we have no position */
    /* But we have valid time from atomic clock */

    char sentence[100];
    snprintf(sentence, sizeof(sentence),
             "$GPGGA,%02d%02d%02d.%02d,0000.0000,N,00000.0000,W,0,00,99.9,0.0,M,0.0,M,,",
             hour, min, sec, centisec);

    char full[104];
    snprintf(full, sizeof(full), "%s*%02X\r\n", sentence, nmea_checksum(sentence));
    nmea_send(full);
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize NMEA output on UART1
 */
void nmea_output_init(void) {
    /* Initialize UART */
    uart_init(NMEA_UART, NMEA_BAUD_RATE);
    gpio_set_function(NMEA_UART_TX, GPIO_FUNC_UART);

    nmea_initialized = true;
    printf("[NMEA] Initialized on GP%d at %d baud\n", NMEA_UART_TX, NMEA_BAUD_RATE);
}

/**
 * NMEA task - call from main loop
 * Outputs NMEA sentences once per PPS
 */
void nmea_output_task(void) {
    if (!nmea_initialized || !nmea_enabled) {
        return;
    }

    /* Check if new PPS has occurred */
    uint32_t pps = g_time_state.pps_count;
    if (pps != last_pps_count) {
        last_pps_count = pps;

        /* Output sentences immediately after PPS */
        nmea_send_gpzda();
        nmea_send_gprmc();
        nmea_send_gpgga();
    }
}

/**
 * Enable/disable NMEA output
 */
void nmea_output_enable(bool enable) {
    nmea_enabled = enable;
    printf("[NMEA] Output %s\n", enable ? "enabled" : "disabled");
}

/**
 * Check if NMEA output is enabled
 */
bool nmea_output_is_enabled(void) {
    return nmea_enabled;
}

/**
 * Get statistics
 */
uint32_t nmea_output_get_count(void) {
    return nmea_sentences_sent;
}
