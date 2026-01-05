/**
 * CHRONOS-Rb Radio Timecode Simulator
 *
 * Generates simulated radio time signal outputs compatible with:
 *   - DCF77 (Germany) - 77.5 kHz on GP2
 *   - WWVB (USA) - 60 kHz on GP3
 *   - JJY40 (Japan Fukushima) - 40 kHz on GP4
 *   - JJY60 (Japan Kyushu) - 60 kHz on GP26
 *
 * These outputs can drive radio-controlled clocks directly or through
 * a small antenna loop for short-range transmission.
 *
 * IMPORTANT: Actual RF transmission requires appropriate licensing.
 * These outputs are intended for direct wired connection or very
 * short-range (<1m) inductive coupling only.
 *
 * Signal encoding:
 *   - Carrier is generated using PWM
 *   - Amplitude modulation via PWM duty cycle
 *   - Time data encoded in pulse width per bit
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "hardware/irq.h"

#include "chronos_rb.h"
#include "radio_timecode.h"

/*============================================================================
 * GPIO ASSIGNMENTS
 *============================================================================*/

#define GPIO_DCF77      2       /* Germany 77.5 kHz */
#define GPIO_WWVB       3       /* USA 60 kHz */
#define GPIO_JJY40      4       /* Japan 40 kHz (Fukushima) */
#define GPIO_JJY60      26      /* Japan 60 kHz (Kyushu) */

/*============================================================================
 * CARRIER FREQUENCIES AND PWM CONFIGURATION
 *============================================================================*/

/* Use SDK's SYS_CLK_HZ (150 MHz) */

/* Target carrier frequencies */
#define DCF77_FREQ_HZ   77500
#define WWVB_FREQ_HZ    60000
#define JJY40_FREQ_HZ   40000
#define JJY60_FREQ_HZ   60000

/* PWM wrap values for each frequency (SYS_CLK / freq - 1) */
#define DCF77_WRAP      (SYS_CLK_HZ / DCF77_FREQ_HZ - 1)     /* 1935 */
#define WWVB_WRAP       (SYS_CLK_HZ / WWVB_FREQ_HZ - 1)      /* 2499 */
#define JJY40_WRAP      (SYS_CLK_HZ / JJY40_FREQ_HZ - 1)     /* 3749 */
#define JJY60_WRAP      (SYS_CLK_HZ / JJY60_FREQ_HZ - 1)     /* 2499 */

/* Amplitude modulation levels (as fraction of wrap) */
#define LEVEL_FULL      100     /* Full carrier (100% duty) */
#define LEVEL_REDUCED   15      /* Reduced carrier for marks (~-17dB) */

/* NTP epoch offset */
#define NTP_UNIX_OFFSET 2208988800UL

/*============================================================================
 * TIME CODE BIT DEFINITIONS
 *============================================================================*/

/* Bit durations in milliseconds */
#define DCF77_BIT0_MS   100     /* Logic 0: 100ms reduced */
#define DCF77_BIT1_MS   200     /* Logic 1: 200ms reduced */
#define WWVB_BIT0_MS    200     /* Logic 0: 200ms reduced */
#define WWVB_BIT1_MS    500     /* Logic 1: 500ms reduced */
#define WWVB_MARKER_MS  800     /* Marker: 800ms reduced */
#define JJY_BIT0_MS     800     /* Logic 0: 800ms full (200ms reduced) */
#define JJY_BIT1_MS     500     /* Logic 1: 500ms full (500ms reduced) */
#define JJY_MARKER_MS   200     /* Marker: 200ms full (800ms reduced) */

/*============================================================================
 * STATE MACHINE
 *============================================================================*/

typedef enum {
    RADIO_STATE_IDLE,
    RADIO_STATE_MINUTE_START,
    RADIO_STATE_SENDING_BIT,
    RADIO_STATE_BIT_COMPLETE
} radio_state_t;

typedef struct {
    bool enabled;
    uint gpio;
    uint slice;
    uint16_t wrap;
    radio_state_t state;
    uint8_t current_bit;        /* Current bit position (0-59) */
    uint8_t bits[60];           /* Encoded bits for this minute */
    uint32_t bit_start_ms;      /* When current bit started */
    uint16_t reduce_duration;   /* How long to reduce carrier (ms) */
} radio_channel_t;

static radio_channel_t dcf77 = {0};
static radio_channel_t wwvb = {0};
static radio_channel_t jjy40 = {0};
static radio_channel_t jjy60 = {0};

static uint32_t last_second = 0;
static uint32_t last_minute = 0;

/*============================================================================
 * TIME CONVERSION HELPERS
 *============================================================================*/

/**
 * Convert NTP timestamp to broken-down UTC time
 */
static void ntp_to_utc(uint32_t ntp_secs, int *year, int *month, int *day,
                       int *hour, int *min, int *sec, int *wday, int *yday) {
    uint32_t unix_time = ntp_secs - NTP_UNIX_OFFSET;
    uint32_t days = unix_time / 86400;
    uint32_t remaining = unix_time % 86400;

    *hour = remaining / 3600;
    *min = (remaining % 3600) / 60;
    *sec = remaining % 60;

    /* Day of week (Jan 1, 1970 was Thursday = 4) */
    *wday = (days + 4) % 7;  /* 0=Sun, 1=Mon, ... */

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
    *yday = doy + 1;  /* 1-based day of year */

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

/**
 * Calculate even parity of BCD value
 */
static uint8_t even_parity(uint8_t *bits, int start, int count) {
    uint8_t parity = 0;
    for (int i = 0; i < count; i++) {
        parity ^= bits[start + i];
    }
    return parity;
}

/*============================================================================
 * DCF77 ENCODING (Germany)
 *============================================================================*/

/**
 * Encode DCF77 time code for the NEXT minute
 * DCF77 transmits data for the minute that will START at second 59
 */
static void dcf77_encode(uint8_t *bits, uint32_t ntp_secs) {
    int year, month, day, hour, min, sec, wday, yday;
    /* Encode for next minute */
    ntp_to_utc(ntp_secs + 60, &year, &month, &day, &hour, &min, &sec, &wday, &yday);

    memset(bits, 0, 60);

    /* Bits 0-14: Reserved (weather data in real DCF77) */

    /* Bit 15: Reserved (call bit) */
    /* Bit 16: Summer time announcement */
    /* Bit 17-18: CEST/CET indicators */
    bits[17] = 0;  /* CET */
    bits[18] = 1;  /* Assume standard time */

    /* Bit 19: Leap second announcement */
    /* Bit 20: Start of time (always 1) */
    bits[20] = 1;

    /* Bits 21-27: Minute (BCD) */
    bits[21] = (min >> 0) & 1;
    bits[22] = (min >> 1) & 1;
    bits[23] = (min >> 2) & 1;
    bits[24] = (min >> 3) & 1;
    bits[25] = (min / 10) & 1;
    bits[26] = ((min / 10) >> 1) & 1;
    bits[27] = ((min / 10) >> 2) & 1;
    /* Bit 28: Minute parity */
    bits[28] = even_parity(bits, 21, 7);

    /* Bits 29-34: Hour (BCD) */
    bits[29] = (hour >> 0) & 1;
    bits[30] = (hour >> 1) & 1;
    bits[31] = (hour >> 2) & 1;
    bits[32] = (hour >> 3) & 1;
    bits[33] = (hour / 10) & 1;
    bits[34] = ((hour / 10) >> 1) & 1;
    /* Bit 35: Hour parity */
    bits[35] = even_parity(bits, 29, 6);

    /* Bits 36-41: Day of month (BCD) */
    bits[36] = (day >> 0) & 1;
    bits[37] = (day >> 1) & 1;
    bits[38] = (day >> 2) & 1;
    bits[39] = (day >> 3) & 1;
    bits[40] = (day / 10) & 1;
    bits[41] = ((day / 10) >> 1) & 1;

    /* Bits 42-44: Day of week (1=Mon, 7=Sun) */
    int dcf_wday = (wday == 0) ? 7 : wday;  /* Convert Sun=0 to Sun=7 */
    bits[42] = (dcf_wday >> 0) & 1;
    bits[43] = (dcf_wday >> 1) & 1;
    bits[44] = (dcf_wday >> 2) & 1;

    /* Bits 45-49: Month (BCD) */
    bits[45] = (month >> 0) & 1;
    bits[46] = (month >> 1) & 1;
    bits[47] = (month >> 2) & 1;
    bits[48] = (month >> 3) & 1;
    bits[49] = (month / 10) & 1;

    /* Bits 50-57: Year within century (BCD) */
    int yr = year % 100;
    bits[50] = (yr >> 0) & 1;
    bits[51] = (yr >> 1) & 1;
    bits[52] = (yr >> 2) & 1;
    bits[53] = (yr >> 3) & 1;
    bits[54] = (yr / 10) & 1;
    bits[55] = ((yr / 10) >> 1) & 1;
    bits[56] = ((yr / 10) >> 2) & 1;
    bits[57] = ((yr / 10) >> 3) & 1;

    /* Bit 58: Date parity */
    bits[58] = even_parity(bits, 36, 22);

    /* Bit 59: No pulse (minute marker) */
    bits[59] = 2;  /* Special: no reduced carrier */
}

/*============================================================================
 * WWVB ENCODING (USA)
 *============================================================================*/

/**
 * Encode WWVB time code
 */
static void wwvb_encode(uint8_t *bits, uint32_t ntp_secs) {
    int year, month, day, hour, min, sec, wday, yday;
    ntp_to_utc(ntp_secs + 60, &year, &month, &day, &hour, &min, &sec, &wday, &yday);
    (void)month; (void)day; (void)wday;

    memset(bits, 0, 60);

    /* Frame reference markers at 0, 9, 19, 29, 39, 49, 59 */
    bits[0] = 2;   /* Marker */
    bits[9] = 2;
    bits[19] = 2;
    bits[29] = 2;
    bits[39] = 2;
    bits[49] = 2;
    /* bits[59] handled as minute marker */

    /* Bits 1-4: Minutes (tens) weighted 40,20,10 */
    bits[1] = (min >= 40) ? 1 : 0;
    bits[2] = ((min % 40) >= 20) ? 1 : 0;
    bits[3] = ((min % 20) >= 10) ? 1 : 0;
    /* Bits 5-8: Minutes (units) weighted 8,4,2,1 */
    int min_units = min % 10;
    bits[5] = (min_units >> 3) & 1;
    bits[6] = (min_units >> 2) & 1;
    bits[7] = (min_units >> 1) & 1;
    bits[8] = (min_units >> 0) & 1;

    /* Bits 12-13: Hours (tens) weighted 20,10 */
    bits[12] = (hour >= 20) ? 1 : 0;
    bits[13] = ((hour % 20) >= 10) ? 1 : 0;
    /* Bits 15-18: Hours (units) weighted 8,4,2,1 */
    int hour_units = hour % 10;
    bits[15] = (hour_units >> 3) & 1;
    bits[16] = (hour_units >> 2) & 1;
    bits[17] = (hour_units >> 1) & 1;
    bits[18] = (hour_units >> 0) & 1;

    /* Bits 22-23: Day of year (hundreds) weighted 200,100 */
    bits[22] = (yday >= 200) ? 1 : 0;
    bits[23] = ((yday % 200) >= 100) ? 1 : 0;
    /* Bits 25-28: Day of year (tens) weighted 80,40,20,10 */
    int doy_tens = (yday % 100);
    bits[25] = (doy_tens >= 80) ? 1 : 0;
    bits[26] = ((doy_tens % 80) >= 40) ? 1 : 0;
    bits[27] = ((doy_tens % 40) >= 20) ? 1 : 0;
    bits[28] = ((doy_tens % 20) >= 10) ? 1 : 0;
    /* Bits 30-33: Day of year (units) weighted 8,4,2,1 */
    int doy_units = yday % 10;
    bits[30] = (doy_units >> 3) & 1;
    bits[31] = (doy_units >> 2) & 1;
    bits[32] = (doy_units >> 1) & 1;
    bits[33] = (doy_units >> 0) & 1;

    /* DUT1 sign and magnitude in bits 36-38, 40-43 (simplified: 0) */

    /* Bits 45-48: Year (tens) weighted 80,40,20,10 */
    int yr = year % 100;
    int yr_tens = yr / 10;
    bits[45] = (yr_tens >> 3) & 1;
    bits[46] = (yr_tens >> 2) & 1;
    bits[47] = (yr_tens >> 1) & 1;
    bits[48] = (yr_tens >> 0) & 1;

    /* Bits 50-53: Year (units) weighted 8,4,2,1 */
    int yr_units = yr % 10;
    bits[50] = (yr_units >> 3) & 1;
    bits[51] = (yr_units >> 2) & 1;
    bits[52] = (yr_units >> 1) & 1;
    bits[53] = (yr_units >> 0) & 1;

    /* Bit 55: Leap year indicator */
    bits[55] = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 1 : 0;

    /* Bit 56: Leap second warning */
    bits[56] = 0;

    /* Bits 57-58: DST indicators */
    bits[57] = 0;
    bits[58] = 0;
}

/*============================================================================
 * JJY ENCODING (Japan)
 *============================================================================*/

/**
 * Encode JJY time code (same for 40kHz and 60kHz)
 */
static void jjy_encode(uint8_t *bits, uint32_t ntp_secs) {
    int year, month, day, hour, min, sec, wday, yday;
    ntp_to_utc(ntp_secs + 60, &year, &month, &day, &hour, &min, &sec, &wday, &yday);
    (void)month; (void)day;

    memset(bits, 0, 60);

    /* Markers at 0, 9, 19, 29, 39, 49, 59 */
    bits[0] = 2;
    bits[9] = 2;
    bits[19] = 2;
    bits[29] = 2;
    bits[39] = 2;
    bits[49] = 2;
    bits[59] = 2;

    /* Minutes (BCD) bits 1-8 */
    bits[1] = (min / 40) & 1;
    bits[2] = ((min % 40) / 20) & 1;
    bits[3] = ((min % 20) / 10) & 1;
    int min_u = min % 10;
    bits[5] = (min_u >> 3) & 1;
    bits[6] = (min_u >> 2) & 1;
    bits[7] = (min_u >> 1) & 1;
    bits[8] = min_u & 1;

    /* Hours (BCD) bits 12-18 */
    bits[12] = (hour / 20) & 1;
    bits[13] = ((hour % 20) / 10) & 1;
    int hour_u = hour % 10;
    bits[15] = (hour_u >> 3) & 1;
    bits[16] = (hour_u >> 2) & 1;
    bits[17] = (hour_u >> 1) & 1;
    bits[18] = hour_u & 1;

    /* Day of year (BCD) bits 22-33 */
    bits[22] = (yday / 200) & 1;
    bits[23] = ((yday % 200) / 100) & 1;
    int doy_t = (yday % 100) / 10;
    bits[25] = (doy_t >> 3) & 1;
    bits[26] = (doy_t >> 2) & 1;
    bits[27] = (doy_t >> 1) & 1;
    bits[28] = doy_t & 1;
    int doy_u = yday % 10;
    bits[30] = (doy_u >> 3) & 1;
    bits[31] = (doy_u >> 2) & 1;
    bits[32] = (doy_u >> 1) & 1;
    bits[33] = doy_u & 1;

    /* Parity bits 36-37 */
    bits[36] = even_parity(bits, 12, 7);  /* Hour parity */
    bits[37] = even_parity(bits, 1, 8);   /* Minute parity */

    /* Year (BCD) bits 41-48 */
    int yr = year % 100;
    int yr_t = yr / 10;
    bits[41] = (yr_t >> 3) & 1;
    bits[42] = (yr_t >> 2) & 1;
    bits[43] = (yr_t >> 1) & 1;
    bits[44] = yr_t & 1;
    int yr_u = yr % 10;
    bits[45] = (yr_u >> 3) & 1;
    bits[46] = (yr_u >> 2) & 1;
    bits[47] = (yr_u >> 1) & 1;
    bits[48] = yr_u & 1;

    /* Day of week bits 50-52 */
    bits[50] = (wday >> 2) & 1;
    bits[51] = (wday >> 1) & 1;
    bits[52] = wday & 1;
}

/*============================================================================
 * PWM CONTROL
 *============================================================================*/

/**
 * Initialize PWM for a radio channel
 */
static void radio_pwm_init(radio_channel_t *ch, uint gpio, uint16_t wrap) {
    ch->gpio = gpio;
    ch->wrap = wrap;
    ch->slice = pwm_gpio_to_slice_num(gpio);

    gpio_set_function(gpio, GPIO_FUNC_PWM);

    pwm_set_wrap(ch->slice, wrap);
    pwm_set_clkdiv(ch->slice, 1.0f);  /* No clock division */
    pwm_set_gpio_level(gpio, wrap / 2);  /* 50% duty = full carrier */
    pwm_set_enabled(ch->slice, true);

    ch->enabled = true;
    ch->state = RADIO_STATE_IDLE;
    ch->current_bit = 0;
}

/**
 * Set carrier amplitude
 * @param level 0-100 (percentage)
 */
static void radio_set_level(radio_channel_t *ch, uint8_t level) {
    if (!ch->enabled) return;

    uint16_t duty = (uint32_t)ch->wrap * level / 200;  /* 50% at full, less when reduced */
    pwm_set_gpio_level(ch->gpio, duty);
}

/*============================================================================
 * BIT TRANSMISSION STATE MACHINE
 *============================================================================*/

/**
 * Start transmitting a bit
 */
static void radio_start_bit(radio_channel_t *ch, uint8_t bit_value,
                            uint16_t zero_ms, uint16_t one_ms, uint16_t marker_ms) {
    ch->state = RADIO_STATE_SENDING_BIT;
    ch->bit_start_ms = to_ms_since_boot(get_absolute_time());

    if (bit_value == 2) {
        /* Marker */
        ch->reduce_duration = marker_ms;
    } else if (bit_value == 1) {
        ch->reduce_duration = one_ms;
    } else {
        ch->reduce_duration = zero_ms;
    }

    /* Start with reduced carrier */
    radio_set_level(ch, LEVEL_REDUCED);
}

/**
 * Update bit transmission state
 */
static void radio_update_bit(radio_channel_t *ch) {
    if (ch->state != RADIO_STATE_SENDING_BIT) return;

    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - ch->bit_start_ms;

    if (elapsed >= ch->reduce_duration) {
        /* Restore full carrier for remainder of second */
        radio_set_level(ch, LEVEL_FULL);
        ch->state = RADIO_STATE_BIT_COMPLETE;
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize all radio timecode outputs
 */
void radio_timecode_init(void) {
    printf("[RADIO] Initializing radio timecode outputs\n");

    radio_pwm_init(&dcf77, GPIO_DCF77, DCF77_WRAP);
    printf("[RADIO] DCF77 (77.5kHz) on GP%d\n", GPIO_DCF77);

    radio_pwm_init(&wwvb, GPIO_WWVB, WWVB_WRAP);
    printf("[RADIO] WWVB (60kHz) on GP%d\n", GPIO_WWVB);

    radio_pwm_init(&jjy40, GPIO_JJY40, JJY40_WRAP);
    printf("[RADIO] JJY40 (40kHz) on GP%d\n", GPIO_JJY40);

    radio_pwm_init(&jjy60, GPIO_JJY60, JJY60_WRAP);
    printf("[RADIO] JJY60 (60kHz) on GP%d\n", GPIO_JJY60);
}

/**
 * Radio timecode task - call frequently from main loop
 */
void radio_timecode_task(void) {
    timestamp_t ts = get_current_time();
    uint32_t ntp_secs = ts.seconds;

    /* Check for new minute - re-encode */
    uint32_t minute = ntp_secs / 60;
    if (minute != last_minute) {
        last_minute = minute;

        /* Encode time for all channels */
        dcf77_encode(dcf77.bits, ntp_secs);
        wwvb_encode(wwvb.bits, ntp_secs);
        jjy_encode(jjy40.bits, ntp_secs);
        memcpy(jjy60.bits, jjy40.bits, 60);  /* Same encoding */

        dcf77.current_bit = 0;
        wwvb.current_bit = 0;
        jjy40.current_bit = 0;
        jjy60.current_bit = 0;
    }

    /* Check for new second - start next bit */
    uint32_t second = ntp_secs % 60;
    if (second != last_second) {
        last_second = second;

        /* Start bit transmission for each channel */
        if (dcf77.enabled && second < 59) {
            radio_start_bit(&dcf77, dcf77.bits[second],
                           DCF77_BIT0_MS, DCF77_BIT1_MS, 0);
        }

        if (wwvb.enabled) {
            radio_start_bit(&wwvb, wwvb.bits[second],
                           WWVB_BIT0_MS, WWVB_BIT1_MS, WWVB_MARKER_MS);
        }

        if (jjy40.enabled) {
            /* JJY: opposite - full carrier, then reduced */
            radio_start_bit(&jjy40, jjy40.bits[second],
                           JJY_BIT0_MS, JJY_BIT1_MS, JJY_MARKER_MS);
        }

        if (jjy60.enabled) {
            radio_start_bit(&jjy60, jjy60.bits[second],
                           JJY_BIT0_MS, JJY_BIT1_MS, JJY_MARKER_MS);
        }
    }

    /* Update bit transmission state machines */
    radio_update_bit(&dcf77);
    radio_update_bit(&wwvb);
    radio_update_bit(&jjy40);
    radio_update_bit(&jjy60);
}

/**
 * Enable/disable individual channels
 */
void radio_timecode_enable(radio_signal_t signal, bool enable) {
    radio_channel_t *ch = NULL;

    switch (signal) {
        case RADIO_DCF77: ch = &dcf77; break;
        case RADIO_WWVB:  ch = &wwvb;  break;
        case RADIO_JJY40: ch = &jjy40; break;
        case RADIO_JJY60: ch = &jjy60; break;
    }

    if (ch) {
        ch->enabled = enable;
        if (!enable) {
            pwm_set_gpio_level(ch->gpio, 0);  /* Turn off carrier */
        } else {
            radio_set_level(ch, LEVEL_FULL);  /* Restore carrier */
        }
    }
}

/**
 * Check if a channel is enabled
 */
bool radio_timecode_is_enabled(radio_signal_t signal) {
    switch (signal) {
        case RADIO_DCF77: return dcf77.enabled;
        case RADIO_WWVB:  return wwvb.enabled;
        case RADIO_JJY40: return jjy40.enabled;
        case RADIO_JJY60: return jjy60.enabled;
    }
    return false;
}

/**
 * Get GPIO pin for a signal
 */
uint8_t radio_timecode_get_gpio(radio_signal_t signal) {
    switch (signal) {
        case RADIO_DCF77: return GPIO_DCF77;
        case RADIO_WWVB:  return GPIO_WWVB;
        case RADIO_JJY40: return GPIO_JJY40;
        case RADIO_JJY60: return GPIO_JJY60;
    }
    return 0;
}
