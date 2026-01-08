/**
 * CHRONOS-Rb AC Mains Frequency Monitor
 *
 * Measures local AC mains frequency from zero-crossing detector input.
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
static float freq_history[AC_FREQ_HISTORY_SIZE];
static uint32_t freq_history_index = 0;
static bool ac_initialized = false;

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
 * Note: Zero-crossing detector fires once per AC cycle
 */
static float period_to_frequency(uint32_t period_us) {
    if (period_us == 0) {
        return 0.0f;
    }

    /* Period is full cycle (one pulse per AC cycle) */
    float period_sec = (float)period_us / 1000000.0f;
    return 1.0f / period_sec;
}

/**
 * Update running average
 */
static void update_average(float new_freq) {
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
    ac_state.frequency_min_hz = 999.0f;
    ac_state.frequency_max_hz = 0.0f;

    /* Configure GPIO for zero-crossing input */
    gpio_init(GPIO_AC_ZERO_CROSS);
    gpio_set_dir(GPIO_AC_ZERO_CROSS, GPIO_IN);
    gpio_pull_up(GPIO_AC_ZERO_CROSS);  /* Pull-up, opto pulls low */

    /* Enable interrupt on falling edge (opto pulls low on zero-cross)
     * Note: Callback is registered in gps_input.c shared handler */
    gpio_set_irq_enabled(GPIO_AC_ZERO_CROSS, GPIO_IRQ_EDGE_FALL, true);

    ac_initialized = true;
    printf("[AC_FREQ] AC frequency monitor initialized on GP%d\n", GPIO_AC_ZERO_CROSS);
}

/**
 * Process AC frequency measurements
 */
void ac_freq_task(void) {
    if (!ac_initialized) {
        return;
    }

    uint32_t now = time_us_32();

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
        update_average(freq);

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

            /* Determine nominal frequency (50 or 60 Hz) */
            float nominal = (ac_state.frequency_avg_hz > 55.0f) ? 60.0f : 50.0f;
            float deviation = ac_state.frequency_avg_hz - nominal;
            printf("  Deviation:   %+.3f Hz from %.0f Hz nominal\n", deviation, nominal);
        }

        printf("  Crossings:   %lu\n", ac_state.zero_cross_count);
        printf("  Period:      %lu us (half-cycle)\n", ac_state.period_us);
    }
    printf("\n");
}
