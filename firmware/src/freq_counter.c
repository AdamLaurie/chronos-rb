/**
 * CHRONOS-Rb Frequency Counter Module
 *
 * Hardware-only validation of PPS against 10MHz reference.
 * PIO counts 10MHz edges between PPS pulses - should be exactly 10,000,000.
 * No CPU involvement in timing-critical path.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "chronos_rb.h"
#include "freq_counter.pio.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Use PIO1 SM0 for frequency counter */
static PIO freq_pio = pio1;
static uint freq_sm = 0;

/* Expected count for 10MHz over 1 second */
#define EXPECTED_COUNT 10000000UL

/* Measurement storage */
static volatile uint32_t last_count = 0;
static volatile uint32_t measurement_count = 0;
static volatile bool new_measurement = false;
static volatile uint64_t last_measurement_time = 0;  /* For timeout detection */

/* Statistics */
static volatile int32_t count_error = 0;        /* Deviation from expected */
static volatile int32_t max_error = 0;
static volatile int32_t min_error = 0;
static volatile uint32_t valid_measurements = 0;
static volatile uint32_t invalid_measurements = 0;

/*============================================================================
 * IRQ HANDLER
 *============================================================================*/

/**
 * PIO IRQ handler - called when measurement is ready
 */
static void freq_counter_irq_handler(void) {
    /* Clear the IRQ (flag 1, not 0 - to avoid conflict with pps_generator) */
    pio_interrupt_clear(freq_pio, 1);

    /* Read count from FIFO */
    uint32_t count;
    if (freq_counter_read(freq_pio, freq_sm, &count)) {
        last_count = count;
        measurement_count++;
        new_measurement = true;
        last_measurement_time = time_us_64();

        /* Calculate error from expected */
        count_error = (int32_t)count - (int32_t)EXPECTED_COUNT;

        /* Update statistics */
        if (measurement_count > 1) {  /* Skip first measurement */
            if (count_error > max_error) max_error = count_error;
            if (count_error < min_error) min_error = count_error;

            /* Check if within tolerance (±10 cycles = ±1µs) */
            if (count_error >= -10 && count_error <= 10) {
                valid_measurements++;
            } else {
                invalid_measurements++;
            }
        }

        /* Update global state */
        g_time_state.last_freq_count = count;
        g_stats.freq_measurements = measurement_count;
    }
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize the frequency counter
 */
void freq_counter_init(void) {
    printf("[FREQ] Initializing hardware frequency counter\n");
    printf("[FREQ] 10MHz input: GPIO %d, PPS input: GPIO %d\n",
           GPIO_10MHZ_INPUT, GPIO_PPS_INPUT);

    /* Add PIO program */
    uint offset = pio_add_program(freq_pio, &freq_counter_program);

    /* Initialize PIO state machine with both pins */
    freq_counter_program_init(freq_pio, freq_sm, offset,
                               GPIO_10MHZ_INPUT, GPIO_PPS_INPUT);

    /* Configure IRQ for measurement notification (using interrupt flag 1) */
    pio_set_irq0_source_enabled(freq_pio, pis_interrupt1, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, freq_counter_irq_handler);
    irq_set_enabled(PIO1_IRQ_0, true);

    /* Start the state machine */
    pio_sm_set_enabled(freq_pio, freq_sm, true);

    printf("[FREQ] PIO counter started, expected count: %lu\n", EXPECTED_COUNT);
    printf("[FREQ] Waiting for PPS signal...\n");
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Get the last frequency count
 */
uint32_t freq_counter_read_count(void) {
    return last_count;
}

/**
 * Check if a new measurement is available
 */
bool freq_counter_new_measurement(void) {
    if (new_measurement) {
        new_measurement = false;
        return true;
    }
    return false;
}

/**
 * Get frequency offset in parts per billion (ppb)
 */
double get_frequency_offset_ppb(void) {
    if (last_count == 0) {
        return 0.0;
    }

    /* Calculate ppb offset from nominal */
    double offset = ((double)last_count - (double)EXPECTED_COUNT) /
                    (double)EXPECTED_COUNT * 1e9;

    g_time_state.frequency_offset = offset;
    return offset;
}

/**
 * Get the count error (deviation from 10,000,000)
 */
int32_t freq_counter_get_error(void) {
    return count_error;
}

/**
 * Get error statistics
 */
void freq_counter_get_stats(int32_t *min_err, int32_t *max_err,
                            uint32_t *valid, uint32_t *invalid) {
    if (min_err) *min_err = min_error;
    if (max_err) *max_err = max_error;
    if (valid) *valid = valid_measurements;
    if (invalid) *invalid = invalid_measurements;
}

/**
 * Get the number of measurements taken
 */
uint32_t freq_counter_get_measurement_count(void) {
    return measurement_count;
}

/**
 * Check if 10MHz signal is present (based on recent valid measurements)
 */
bool freq_counter_signal_present(void) {
    /* Signal is present if we have recent valid measurements */
    if (measurement_count == 0) {
        return false;  /* Never received a measurement */
    }

    /* Check if measurement is recent (within last 2 seconds) */
    uint64_t now = time_us_64();
    uint64_t age = now - last_measurement_time;
    if (age > 2000000) {
        return false;  /* Measurement is stale - signal lost */
    }

    /* Check if count is reasonable (within 10% of 10MHz) */
    return (last_count > 9000000 && last_count < 11000000);
}

/**
 * Reset statistics
 */
void freq_counter_reset_stats(void) {
    max_error = 0;
    min_error = 0;
    valid_measurements = 0;
    invalid_measurements = 0;
}

/*============================================================================
 * LEGACY API (for compatibility)
 *============================================================================*/

/**
 * Legacy read function - returns last count
 * Note: Name differs from PIO inline function freq_counter_read(pio, sm, count)
 */
uint32_t freq_counter_read_legacy(void) {
    return last_count;
}

/**
 * Legacy PPS start function - now a no-op since PIO handles everything
 */
void freq_counter_pps_start(void) {
    /* No-op: PIO automatically captures on PPS edge */
}
