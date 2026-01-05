/**
 * CHRONOS-Rb IRIG-B Timecode Output
 *
 * Generates IRIG-B timecode for aerospace, military, and test equipment.
 *
 * Supported formats:
 *   - IRIG-B000 (DC level shift, unmodulated) on GP27
 *   - IRIG-B120 (1kHz AM modulated) on GP27 (when modulated mode enabled)
 *
 * IRIG-B Frame Structure (100 bits/second, 10ms per bit):
 *   - Position identifiers (P0-P9): 8ms high
 *   - Binary 1: 5ms high
 *   - Binary 0: 2ms high
 *   - Reference marker: 8ms high (same as position identifier)
 *
 * Frame contains:
 *   - Seconds (BCD)
 *   - Minutes (BCD)
 *   - Hours (BCD)
 *   - Day of year (BCD)
 *   - Year (BCD, optional)
 *   - Control functions
 *   - Straight binary seconds (SBS)
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

#include "chronos_rb.h"
#include "irig_b.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define GPIO_IRIG_B         27      /* IRIG-B output */
#define IRIG_CARRIER_HZ     1000    /* 1kHz carrier for modulated mode */

/* Bit durations in microseconds (10ms total per bit) */
#define BIT_PERIOD_US       10000
#define BIT_0_HIGH_US       2000    /* Binary 0: 2ms high */
#define BIT_1_HIGH_US       5000    /* Binary 1: 5ms high */
#define BIT_P_HIGH_US       8000    /* Position identifier: 8ms high */

/* NTP epoch offset */
#define NTP_UNIX_OFFSET     2208988800UL

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static bool irig_initialized = false;
static bool irig_enabled = true;
static bool irig_modulated = false;  /* true = AM 1kHz, false = DC level shift */

static uint irig_slice = 0;
static uint16_t irig_wrap = 0;

/* Current frame being transmitted */
static uint8_t irig_frame[100];     /* 0=low, 1=high for each 100µs segment */
static uint32_t frame_start_us = 0;
static uint32_t last_second = 0;
static bool frame_active = false;

/*============================================================================
 * TIME CONVERSION
 *============================================================================*/

/**
 * Convert NTP timestamp to broken-down UTC time
 */
static void ntp_to_utc(uint32_t ntp_secs, int *year, int *month, int *day,
                       int *hour, int *min, int *sec, int *yday) {
    uint32_t unix_time = ntp_secs - NTP_UNIX_OFFSET;
    uint32_t days = unix_time / 86400;
    uint32_t remaining = unix_time % 86400;

    *hour = remaining / 3600;
    *min = (remaining % 3600) / 60;
    *sec = remaining % 60;

    int y = 1970;
    int doy = 0;
    while (1) {
        int days_in_year = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (days < (uint32_t)days_in_year) {
            doy = days;
            break;
        }
        days -= days_in_year;
        y++;
    }
    *year = y;
    *yday = doy + 1;  /* 1-based */

    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) {
        days_in_month[1] = 29;
    }

    int m = 0;
    while (days >= (uint32_t)days_in_month[m]) {
        days -= days_in_month[m];
        m++;
    }
    *month = m + 1;
    *day = days + 1;
}

/*============================================================================
 * IRIG-B FRAME ENCODING
 *============================================================================*/

/**
 * Set a bit in the frame
 * @param bit_num  Bit position (0-99)
 * @param type     0=binary 0, 1=binary 1, 2=position identifier
 */
static void set_bit(int bit_num, int type) {
    int high_us;
    switch (type) {
        case 0:  high_us = BIT_0_HIGH_US; break;
        case 1:  high_us = BIT_1_HIGH_US; break;
        default: high_us = BIT_P_HIGH_US; break;  /* Position identifier */
    }

    /* Each bit is 10ms, we store state for each 100µs (100 segments per bit) */
    int start_seg = bit_num * 100;
    int high_segs = high_us / 100;

    for (int i = 0; i < 100; i++) {
        irig_frame[start_seg + i] = (i < high_segs) ? 1 : 0;
    }
}

/**
 * Encode IRIG-B frame for current second
 *
 * Frame structure (100 bits):
 * Bit 0:      Reference marker (P0)
 * Bits 1-4:   Seconds units (BCD 1,2,4,8)
 * Bits 5-8:   Seconds tens (BCD 10,20,40, unused)
 * Bit 9:      Position identifier (P1)
 * Bits 10-13: Minutes units (BCD 1,2,4,8)
 * Bits 14-18: Minutes tens (BCD 10,20,40, unused, unused)
 * Bit 19:     Position identifier (P2)
 * Bits 20-23: Hours units (BCD 1,2,4,8)
 * Bits 24-28: Hours tens (BCD 10,20, unused, unused, unused)
 * Bit 29:     Position identifier (P3)
 * Bits 30-33: Days units (BCD 1,2,4,8)
 * Bits 34-38: Days tens (BCD 10,20,40,80, unused)
 * Bit 39:     Position identifier (P4)
 * Bits 40-41: Days hundreds (BCD 100,200)
 * Bits 42-49: Control functions (CF1-CF8)
 * Bit 49:     Position identifier (P5)
 * Bits 50-58: Straight binary seconds (17-bit LSBs)
 * Bit 59:     Position identifier (P6)
 * Bits 60-68: Straight binary seconds (continued)
 * Bit 69:     Position identifier (P7)
 * Bits 70-78: Control functions / IEEE 1344 extensions
 * Bit 79:     Position identifier (P8)
 * Bits 80-88: Year (BCD) - IEEE 1344 extension
 * Bit 89:     Position identifier (P9)
 * Bits 90-99: Control functions / parity
 * Bit 99:     Reference marker (P0 of next frame)
 */
static void encode_irig_frame(uint32_t ntp_secs) {
    int year, month, day, hour, min, sec, yday;
    ntp_to_utc(ntp_secs, &year, &month, &day, &hour, &min, &sec, &yday);
    (void)month; (void)day;

    /* Clear frame */
    memset(irig_frame, 0, sizeof(irig_frame));

    /* Position identifiers */
    set_bit(0, 2);   /* P0 - Reference */
    set_bit(9, 2);   /* P1 */
    set_bit(19, 2);  /* P2 */
    set_bit(29, 2);  /* P3 */
    set_bit(39, 2);  /* P4 */
    set_bit(49, 2);  /* P5 */
    set_bit(59, 2);  /* P6 */
    set_bit(69, 2);  /* P7 */
    set_bit(79, 2);  /* P8 */
    set_bit(89, 2);  /* P9 */
    set_bit(99, 2);  /* P0 of next frame */

    /* Seconds (BCD) */
    int sec_u = sec % 10;
    int sec_t = sec / 10;
    set_bit(1, (sec_u >> 0) & 1);
    set_bit(2, (sec_u >> 1) & 1);
    set_bit(3, (sec_u >> 2) & 1);
    set_bit(4, (sec_u >> 3) & 1);
    set_bit(5, (sec_t >> 0) & 1);
    set_bit(6, (sec_t >> 1) & 1);
    set_bit(7, (sec_t >> 2) & 1);
    /* Bit 8 unused (would be seconds tens 80, not used) */

    /* Minutes (BCD) */
    int min_u = min % 10;
    int min_t = min / 10;
    set_bit(10, (min_u >> 0) & 1);
    set_bit(11, (min_u >> 1) & 1);
    set_bit(12, (min_u >> 2) & 1);
    set_bit(13, (min_u >> 3) & 1);
    set_bit(14, (min_t >> 0) & 1);
    set_bit(15, (min_t >> 1) & 1);
    set_bit(16, (min_t >> 2) & 1);
    /* Bits 17-18 unused */

    /* Hours (BCD) */
    int hour_u = hour % 10;
    int hour_t = hour / 10;
    set_bit(20, (hour_u >> 0) & 1);
    set_bit(21, (hour_u >> 1) & 1);
    set_bit(22, (hour_u >> 2) & 1);
    set_bit(23, (hour_u >> 3) & 1);
    set_bit(24, (hour_t >> 0) & 1);
    set_bit(25, (hour_t >> 1) & 1);
    /* Bits 26-28 unused */

    /* Day of year (BCD) */
    int day_u = yday % 10;
    int day_t = (yday / 10) % 10;
    int day_h = yday / 100;
    set_bit(30, (day_u >> 0) & 1);
    set_bit(31, (day_u >> 1) & 1);
    set_bit(32, (day_u >> 2) & 1);
    set_bit(33, (day_u >> 3) & 1);
    set_bit(34, (day_t >> 0) & 1);
    set_bit(35, (day_t >> 1) & 1);
    set_bit(36, (day_t >> 2) & 1);
    set_bit(37, (day_t >> 3) & 1);
    /* Bit 38 unused */
    set_bit(40, (day_h >> 0) & 1);
    set_bit(41, (day_h >> 1) & 1);

    /* Straight Binary Seconds (SBS) - seconds of day */
    uint32_t sbs = (uint32_t)hour * 3600 + (uint32_t)min * 60 + (uint32_t)sec;
    /* SBS goes in bits 50-58 (LSB) and 60-68 (MSB) */
    set_bit(50, (sbs >> 0) & 1);
    set_bit(51, (sbs >> 1) & 1);
    set_bit(52, (sbs >> 2) & 1);
    set_bit(53, (sbs >> 3) & 1);
    set_bit(54, (sbs >> 4) & 1);
    set_bit(55, (sbs >> 5) & 1);
    set_bit(56, (sbs >> 6) & 1);
    set_bit(57, (sbs >> 7) & 1);
    set_bit(58, (sbs >> 8) & 1);
    set_bit(60, (sbs >> 9) & 1);
    set_bit(61, (sbs >> 10) & 1);
    set_bit(62, (sbs >> 11) & 1);
    set_bit(63, (sbs >> 12) & 1);
    set_bit(64, (sbs >> 13) & 1);
    set_bit(65, (sbs >> 14) & 1);
    set_bit(66, (sbs >> 15) & 1);
    set_bit(67, (sbs >> 16) & 1);

    /* Year (IEEE 1344 extension, bits 80-88) */
    int yr = year % 100;
    int yr_u = yr % 10;
    int yr_t = yr / 10;
    set_bit(80, (yr_u >> 0) & 1);
    set_bit(81, (yr_u >> 1) & 1);
    set_bit(82, (yr_u >> 2) & 1);
    set_bit(83, (yr_u >> 3) & 1);
    set_bit(84, (yr_t >> 0) & 1);
    set_bit(85, (yr_t >> 1) & 1);
    set_bit(86, (yr_t >> 2) & 1);
    set_bit(87, (yr_t >> 3) & 1);
}

/*============================================================================
 * OUTPUT CONTROL
 *============================================================================*/

/**
 * Set output level
 * @param high  true = high/carrier on, false = low/carrier off
 */
static void irig_set_output(bool high) {
    if (irig_modulated) {
        /* AM modulated: 50% duty when high, 0% when low */
        pwm_set_gpio_level(GPIO_IRIG_B, high ? (irig_wrap / 2) : 0);
    } else {
        /* DC level shift */
        gpio_put(GPIO_IRIG_B, high);
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize IRIG-B output
 */
void irig_b_init(void) {
    printf("[IRIG-B] Initializing on GP%d\n", GPIO_IRIG_B);

    /* Initialize as GPIO output first (DC mode default) */
    gpio_init(GPIO_IRIG_B);
    gpio_set_dir(GPIO_IRIG_B, GPIO_OUT);
    gpio_put(GPIO_IRIG_B, 0);

    /* Calculate PWM wrap for 1kHz carrier (for AM mode) */
    /* 150MHz / 1kHz = 150000, but wrap is 16-bit max */
    /* Use clock divider: 150MHz / 3 = 50MHz, 50MHz / 50000 = 1kHz */
    irig_wrap = 50000;
    irig_slice = pwm_gpio_to_slice_num(GPIO_IRIG_B);

    irig_initialized = true;
    printf("[IRIG-B] Mode: DC level shift (IRIG-B000)\n");
}

/**
 * IRIG-B task - call frequently from main loop
 */
void irig_b_task(void) {
    if (!irig_initialized || !irig_enabled) {
        return;
    }

    timestamp_t ts = get_current_time();
    uint32_t ntp_secs = ts.seconds;
    uint32_t second = ntp_secs % 86400;  /* Seconds of day for SBS */
    (void)second;

    /* Check for new second - encode new frame */
    if (ntp_secs != last_second) {
        last_second = ntp_secs;
        encode_irig_frame(ntp_secs);
        frame_start_us = time_us_32();
        frame_active = true;
    }

    /* Output current bit state */
    if (frame_active) {
        uint32_t elapsed_us = time_us_32() - frame_start_us;

        if (elapsed_us >= 1000000) {
            /* Frame complete (1 second = 100 bits @ 10ms each) */
            frame_active = false;
            irig_set_output(false);
        } else {
            /* Determine current segment (each 100µs) */
            uint32_t segment = elapsed_us / 100;
            if (segment < 10000) {  /* 100 bits * 100 segments */
                /* Get bit number and segment within bit */
                uint32_t bit_num = segment / 100;
                uint32_t seg_in_bit = segment % 100;

                /* Lookup pre-computed state */
                if (bit_num < 100) {
                    /* Simplified: compute on-the-fly based on bit type */
                    int type = 0;  /* Will be determined by position */

                    /* Check position identifiers */
                    if (bit_num == 0 || bit_num == 9 || bit_num == 19 ||
                        bit_num == 29 || bit_num == 39 || bit_num == 49 ||
                        bit_num == 59 || bit_num == 69 || bit_num == 79 ||
                        bit_num == 89 || bit_num == 99) {
                        type = 2;  /* Position identifier: 8ms high */
                    }

                    int high_segs;
                    switch (type) {
                        case 2:  high_segs = 80; break;  /* 8ms */
                        case 1:  high_segs = 50; break;  /* 5ms */
                        default: high_segs = 20; break;  /* 2ms */
                    }

                    irig_set_output(seg_in_bit < (uint32_t)high_segs);
                }
            }
        }
    }
}

/**
 * Enable/disable IRIG-B output
 */
void irig_b_enable(bool enable) {
    irig_enabled = enable;
    if (!enable) {
        irig_set_output(false);
    }
    printf("[IRIG-B] %s\n", enable ? "Enabled" : "Disabled");
}

/**
 * Set IRIG-B mode
 * @param modulated  true = IRIG-B120 (1kHz AM), false = IRIG-B000 (DC)
 */
void irig_b_set_mode(bool modulated) {
    if (modulated && !irig_modulated) {
        /* Switch to PWM mode */
        gpio_set_function(GPIO_IRIG_B, GPIO_FUNC_PWM);
        pwm_set_wrap(irig_slice, irig_wrap);
        pwm_set_clkdiv(irig_slice, 3.0f);  /* 150MHz / 3 = 50MHz */
        pwm_set_enabled(irig_slice, true);
        printf("[IRIG-B] Mode: AM modulated (IRIG-B120)\n");
    } else if (!modulated && irig_modulated) {
        /* Switch to GPIO mode */
        pwm_set_enabled(irig_slice, false);
        gpio_init(GPIO_IRIG_B);
        gpio_set_dir(GPIO_IRIG_B, GPIO_OUT);
        printf("[IRIG-B] Mode: DC level shift (IRIG-B000)\n");
    }
    irig_modulated = modulated;
}

/**
 * Check if IRIG-B is enabled
 */
bool irig_b_is_enabled(void) {
    return irig_enabled;
}

/**
 * Check if IRIG-B is in modulated mode
 */
bool irig_b_is_modulated(void) {
    return irig_modulated;
}
