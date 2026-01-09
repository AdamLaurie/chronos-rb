/**
 * CHRONOS-Rb AC Mains Frequency Monitor
 *
 * Measures local AC mains frequency from zero-crossing detector input.
 * Implements hierarchical averaging:
 *   - Instant: ~1 second rolling average
 *   - Minute history: 60 samples (1 per minute, averaged from 60 seconds)
 *   - Hour history: 48 samples (1 per hour, averaged from 60 minutes)
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "chronos_rb.h"
#include "ac_freq_monitor.h"

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static ac_freq_state_t ac_state = {0};
static bool ac_initialized = false;

/* Short-term history for instant average (~1 second) */
static float freq_history[AC_FREQ_HISTORY_SIZE];
static uint32_t freq_history_index = 0;

/* Hierarchical averaging accumulators */
static float second_accum = 0.0f;       /* Accumulator for current second */
static uint32_t second_count = 0;       /* Samples in current second */
static float minute_accum = 0.0f;       /* Accumulator for current minute */
static uint32_t minute_count = 0;       /* Seconds in current minute */

/* Minute history (last 60 minutes) */
static float minute_history[AC_FREQ_MINUTE_HISTORY];
static uint32_t minute_history_index = 0;
static uint32_t minute_history_count = 0;  /* Valid samples in buffer */

/* Hour history (last 48 hours) */
static float hour_history[AC_FREQ_HOUR_HISTORY];
static uint32_t hour_history_index = 0;
static uint32_t hour_history_count = 0;    /* Valid samples in buffer */

/* Hour accumulator */
static float hour_accum = 0.0f;         /* Accumulator for current hour */
static uint32_t hour_minute_count = 0;  /* Minutes in current hour */

/* Timing for hierarchical rollover */
static uint32_t last_second_time = 0;
static uint32_t last_minute_time = 0;
static uint32_t last_hour_time = 0;

/* Edge timing for frequency calculation */
static volatile uint32_t last_edge_us = 0;
static volatile uint32_t edge_period_us = 0;
static volatile uint32_t edge_count = 0;

/*============================================================================
 * INTERRUPT HANDLER
 *============================================================================*/

/**
 * AC zero-crossing IRQ handler - called from shared GPIO callback
 */
void ac_zero_cross_irq_handler(void) {
    uint32_t now = time_us_32();

    /* Calculate period from previous edge */
    if (last_edge_us != 0) {
        edge_period_us = now - last_edge_us;
    }

    last_edge_us = now;
    edge_count++;
}

/*============================================================================
 * PRIVATE FUNCTIONS
 *============================================================================*/

/**
 * Calculate frequency from period
 */
static float period_to_frequency(uint32_t period_us) {
    if (period_us == 0) {
        return 0.0f;
    }
    float period_sec = (float)period_us / 1000000.0f;
    return 1.0f / period_sec;
}

/**
 * Update short-term running average
 */
static void update_instant_average(float new_freq) {
    freq_history[freq_history_index] = new_freq;
    freq_history_index = (freq_history_index + 1) % AC_FREQ_HISTORY_SIZE;

    /* Calculate average */
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < AC_FREQ_HISTORY_SIZE; i++) {
        if (freq_history[i] > 0.0f) {
            sum += freq_history[i];
            count++;
        }
    }

    if (count > 0) {
        ac_state.frequency_avg_hz = sum / (float)count;
    }
}

/**
 * Process hierarchical averaging
 * Called with each valid frequency measurement
 */
static void update_hierarchical(float freq, uint32_t now_ms) {
    /* Accumulate for second average */
    second_accum += freq;
    second_count++;

    /* Check for second rollover (every 1000ms) */
    if (now_ms - last_second_time >= 1000) {
        if (second_count > 0) {
            float sec_avg = second_accum / (float)second_count;

            /* Add to minute accumulator */
            minute_accum += sec_avg;
            minute_count++;
        }

        /* Reset second accumulator */
        second_accum = 0.0f;
        second_count = 0;
        last_second_time = now_ms;
    }

    /* Check for minute rollover (every 60 seconds) */
    if (now_ms - last_minute_time >= 60000) {
        if (minute_count > 0) {
            float min_avg = minute_accum / (float)minute_count;

            /* Store in minute history */
            minute_history[minute_history_index] = min_avg;
            minute_history_index = (minute_history_index + 1) % AC_FREQ_MINUTE_HISTORY;
            if (minute_history_count < AC_FREQ_MINUTE_HISTORY) {
                minute_history_count++;
            }

            /* Add to hour accumulator */
            hour_accum += min_avg;
            hour_minute_count++;
        }

        /* Reset minute accumulator */
        minute_accum = 0.0f;
        minute_count = 0;
        last_minute_time = now_ms;
    }

    /* Check for hour rollover (every 60 minutes) */
    if (now_ms - last_hour_time >= 3600000) {
        if (hour_minute_count > 0) {
            float hour_avg = hour_accum / (float)hour_minute_count;

            /* Store in hour history */
            hour_history[hour_history_index] = hour_avg;
            hour_history_index = (hour_history_index + 1) % AC_FREQ_HOUR_HISTORY;
            if (hour_history_count < AC_FREQ_HOUR_HISTORY) {
                hour_history_count++;
            }
        }

        /* Reset hour accumulator */
        hour_accum = 0.0f;
        hour_minute_count = 0;
        last_hour_time = now_ms;
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize AC frequency monitor
 */
void ac_freq_init(void) {
    /* Initialize state */
    memset(&ac_state, 0, sizeof(ac_state));
    memset(freq_history, 0, sizeof(freq_history));
    memset(minute_history, 0, sizeof(minute_history));
    memset(hour_history, 0, sizeof(hour_history));
    ac_state.frequency_min_hz = 999.0f;
    ac_state.frequency_max_hz = 0.0f;

    /* Initialize timing */
    uint32_t now = time_us_32() / 1000;
    last_second_time = now;
    last_minute_time = now;
    last_hour_time = now;

    /* Configure GPIO for zero-crossing input */
    gpio_init(GPIO_AC_ZERO_CROSS);
    gpio_set_dir(GPIO_AC_ZERO_CROSS, GPIO_IN);
    gpio_pull_up(GPIO_AC_ZERO_CROSS);

    /* Enable interrupt on falling edge */
    gpio_set_irq_enabled(GPIO_AC_ZERO_CROSS, GPIO_IRQ_EDGE_FALL, true);

    ac_initialized = true;
    printf("[AC_FREQ] AC frequency monitor initialized on GP%d\n", GPIO_AC_ZERO_CROSS);
    printf("[AC_FREQ] History: %d min + %d hour samples\n",
           AC_FREQ_MINUTE_HISTORY, AC_FREQ_HOUR_HISTORY);
}

/**
 * Process AC frequency measurements
 */
void ac_freq_task(void) {
    if (!ac_initialized) {
        return;
    }

    uint32_t now = time_us_32();
    uint32_t now_ms = now / 1000;

    /* Copy volatile values atomically */
    uint32_t period = edge_period_us;
    uint32_t last_edge = last_edge_us;
    uint32_t count = edge_count;

    /* Check for signal timeout */
    if (last_edge != 0 && (now - last_edge) > (AC_FREQ_TIMEOUT_MS * 1000)) {
        ac_state.signal_present = false;
        ac_state.frequency_valid = false;
        ac_state.frequency_hz = 0.0f;
        return;
    }

    /* Check if we have valid measurements */
    if (period == 0 || last_edge == 0) {
        ac_state.signal_present = false;
        return;
    }

    ac_state.signal_present = true;
    ac_state.zero_cross_count = count;
    ac_state.last_edge_time_us = last_edge;
    ac_state.period_us = period;

    /* Calculate frequency */
    float freq = period_to_frequency(period);
    ac_state.frequency_hz = freq;

    /* Validate frequency range */
    if (freq >= AC_FREQ_MIN_HZ && freq <= AC_FREQ_MAX_HZ) {
        ac_state.frequency_valid = true;
        update_instant_average(freq);
        update_hierarchical(freq, now_ms);

        /* Update min/max tracking */
        if (freq < ac_state.frequency_min_hz) {
            ac_state.frequency_min_hz = freq;
        }
        if (freq > ac_state.frequency_max_hz) {
            ac_state.frequency_max_hz = freq;
        }
    } else {
        ac_state.frequency_valid = false;
    }
}

/**
 * Get current AC mains frequency
 */
float ac_freq_get_hz(void) {
    if (!ac_state.frequency_valid) {
        return 0.0f;
    }
    return ac_state.frequency_hz;
}

/**
 * Get averaged AC mains frequency
 */
float ac_freq_get_avg_hz(void) {
    if (!ac_state.frequency_valid) {
        return 0.0f;
    }
    return ac_state.frequency_avg_hz;
}

/**
 * Check if AC frequency measurement is valid
 */
bool ac_freq_is_valid(void) {
    return ac_state.frequency_valid;
}

/**
 * Check if zero-crossing signal is present
 */
bool ac_freq_signal_present(void) {
    return ac_state.signal_present;
}

/**
 * Get full AC frequency state
 */
const ac_freq_state_t* ac_freq_get_state(void) {
    return &ac_state;
}

/**
 * Get minute history (oldest first)
 */
int ac_freq_get_minute_history(float *buf, int max_samples) {
    if (minute_history_count == 0) {
        return 0;
    }

    int count = (minute_history_count < max_samples) ? minute_history_count : max_samples;
    int start;

    if (minute_history_count < AC_FREQ_MINUTE_HISTORY) {
        /* Buffer not full yet, start from 0 */
        start = 0;
    } else {
        /* Buffer full, oldest is at current index */
        start = minute_history_index;
    }

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % AC_FREQ_MINUTE_HISTORY;
        buf[i] = minute_history[idx];
    }

    return count;
}

/**
 * Get hour history (oldest first)
 */
int ac_freq_get_hour_history(float *buf, int max_samples) {
    if (hour_history_count == 0) {
        return 0;
    }

    int count = (hour_history_count < max_samples) ? hour_history_count : max_samples;
    int start;

    if (hour_history_count < AC_FREQ_HOUR_HISTORY) {
        /* Buffer not full yet, start from 0 */
        start = 0;
    } else {
        /* Buffer full, oldest is at current index */
        start = hour_history_index;
    }

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % AC_FREQ_HOUR_HISTORY;
        buf[i] = hour_history[idx];
    }

    return count;
}

/**
 * Get accumulator status for diagnostics
 */
void ac_freq_get_accum_status(uint32_t *sec_count_out, uint32_t *min_count_out) {
    if (sec_count_out) *sec_count_out = second_count;
    if (min_count_out) *min_count_out = minute_count;
}

/**
 * Print AC frequency status to console
 */
void ac_freq_print_status(void) {
    printf("\nAC Mains Frequency Monitor:\n");
    printf("  Signal:      %s\n", ac_state.signal_present ? "Present" : "Not detected");

    if (ac_state.signal_present) {
        printf("  Frequency:   %.3f Hz\n", ac_state.frequency_hz);
        printf("  Average:     %.3f Hz\n", ac_state.frequency_avg_hz);
        printf("  Valid:       %s\n", ac_state.frequency_valid ? "Yes" : "No (out of range)");

        if (ac_state.frequency_valid) {
            printf("  Min:         %.3f Hz\n", ac_state.frequency_min_hz);
            printf("  Max:         %.3f Hz\n", ac_state.frequency_max_hz);

            float nominal = (ac_state.frequency_avg_hz > 55.0f) ? 60.0f : 50.0f;
            float deviation = ac_state.frequency_avg_hz - nominal;
            printf("  Deviation:   %+.3f Hz from %.0f Hz nominal\n", deviation, nominal);
        }

        printf("  Crossings:   %lu\n", ac_state.zero_cross_count);
        printf("  History:     %u min, %u hour samples\n",
               minute_history_count, hour_history_count);
    }
    printf("\n");
}
