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
#include "hardware/gpio.h"

#include "chronos_rb.h"
#include "freq_counter.pio.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* PIO1 state machine allocation:
 *   SM0: freq_counter - counts 10MHz between FE PPS pulses
 *   SM1: (unused - appears broken, doesn't produce FIFO data)
 *   SM2: fe_pps_capture - captures 10MHz count at FE PPS edge
 *   SM3: gps_pps_capture - captures 10MHz count at GPS PPS edge
 */
static PIO freq_pio = pio1;
static uint freq_sm = 0;
static uint fe_pps_sm = 2;   /* SM2 works for FE PPS */
static uint gps_pps_sm = 3;  /* Try SM3 - SM1 appears broken */

/* Expected count for 10MHz over 1 second */
#define EXPECTED_COUNT 10000000UL

/* PPS capture counts (from PIO, no IRQ latency) */
static volatile uint32_t fe_pps_capture_count = 0;
static volatile uint32_t gps_pps_capture_count = 0;
static volatile bool fe_pps_capture_valid = false;
static volatile bool gps_pps_capture_valid = false;

/* PPS offset statistics */
#define PPS_OFFSET_HISTORY_SIZE 60  /* 60 seconds of history */
static int32_t pps_offset_history[PPS_OFFSET_HISTORY_SIZE];
static uint32_t pps_offset_history_idx = 0;
static uint32_t pps_offset_history_count = 0;
static int32_t pps_offset_last = 0;
static int32_t pps_offset_prev = 0;
static double pps_drift_rate = 0.0;      /* ticks per second */
static double pps_offset_stddev = 0.0;   /* standard deviation in ticks */

/* PIO latency compensation: cycles lost due to edge detection and loop overhead.
 * The PIO loses ~9 cycles between detecting PPS rise and starting to count,
 * plus asymmetry between start/end detection. Measured empirically. */
#define PIO_LATENCY_COMPENSATION 9

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
 * PIO IRQ handler - called when frequency measurement is ready
 */
static void freq_counter_irq_handler(void) {
    /* Clear the IRQ (flag 1, not 0 - to avoid conflict with pps_generator) */
    pio_interrupt_clear(freq_pio, 1);

    /* Read count from FIFO and apply latency compensation */
    uint32_t raw_count;
    if (freq_counter_read(freq_pio, freq_sm, &raw_count)) {
        uint32_t count = raw_count + PIO_LATENCY_COMPENSATION;
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
    printf("[FREQ] 10MHz input: GPIO %d, Rb PPS: GPIO %d, GPS PPS: GPIO %d\n",
           GPIO_10MHZ_INPUT, GPIO_PPS_INPUT, GPIO_GPS_PPS_INPUT);

    /* Ensure GPS PPS pin is configured as input (needed before gps_input_init) */
    gpio_init(GPIO_GPS_PPS_INPUT);
    gpio_set_dir(GPIO_GPS_PPS_INPUT, GPIO_IN);

    /* Add PIO programs */
    uint offset = pio_add_program(freq_pio, &freq_counter_program);
    uint pps_capture_offset = pio_add_program(freq_pio, &pps_capture_program);

    /* Initialize main frequency counter (SM0) - counts 10MHz between FE PPS */
    freq_counter_program_init(freq_pio, freq_sm, offset,
                               GPIO_10MHZ_INPUT, GPIO_PPS_INPUT);

    /* Configure IRQ for frequency measurement notification (using interrupt flag 1) */
    pio_set_irq0_source_enabled(freq_pio, pis_interrupt1, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, freq_counter_irq_handler);
    irq_set_enabled(PIO1_IRQ_0, true);

    /* Start the main frequency counter state machine FIRST */
    pio_sm_set_enabled(freq_pio, freq_sm, true);

    /* Initialize GPS PPS capture (SM2) - captures 10MHz count at GPS PPS edge */
    pps_capture_program_init(freq_pio, gps_pps_sm, pps_capture_offset,
                              GPIO_10MHZ_INPUT, GPIO_GPS_PPS_INPUT);
    pio_sm_set_enabled(freq_pio, gps_pps_sm, true);

    /* Initialize FE PPS capture (SM1) AFTER SM0 is running
     * Both share GP21 for JMP pin - ensure SM0 is stable first */
    pps_capture_program_init(freq_pio, fe_pps_sm, pps_capture_offset,
                              GPIO_10MHZ_INPUT, GPIO_PPS_INPUT);
    pio_sm_set_enabled(freq_pio, fe_pps_sm, true);

    printf("[FREQ] PIO counter started, expected count: %lu\n", EXPECTED_COUNT);
    printf("[FREQ] PPS capture SMs: Rb=SM2, GPS=SM3 (SM1 broken)\n");
    printf("[FREQ] Waiting for PPS signals...\n");
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

/*============================================================================
 * PPS OFFSET MEASUREMENT (10MHz locked, no IRQ latency)
 *============================================================================*/

/**
 * Update PPS offset statistics
 * Called when we have both FE and GPS PPS captures
 */
static void update_pps_offset_stats(void) {
    /* Calculate offset: GPS count - FE count (raw, no normalization needed).
     * The absolute value is arbitrary since both counters started at different times.
     * What matters is drift (change over time) and jitter (stability). */
    int32_t offset = (int32_t)(gps_pps_capture_count - fe_pps_capture_count);

    /* Store previous offset for drift calculation */
    pps_offset_prev = pps_offset_last;
    pps_offset_last = offset;

    /* Add to history */
    pps_offset_history[pps_offset_history_idx] = offset;
    pps_offset_history_idx = (pps_offset_history_idx + 1) % PPS_OFFSET_HISTORY_SIZE;
    if (pps_offset_history_count < PPS_OFFSET_HISTORY_SIZE) {
        pps_offset_history_count++;
    }

    /* Calculate drift rate (ticks per second) */
    if (pps_offset_history_count >= 2) {
        /* Drift = change in offset from previous sample */
        int32_t drift = offset - pps_offset_prev;
        /* Exponential moving average for smoothing */
        pps_drift_rate = pps_drift_rate * 0.9 + (double)drift * 0.1;
    }

    /* Calculate standard deviation */
    if (pps_offset_history_count >= 2) {
        /* Calculate mean */
        double sum = 0.0;
        for (uint32_t i = 0; i < pps_offset_history_count; i++) {
            sum += pps_offset_history[i];
        }
        double mean = sum / pps_offset_history_count;

        /* Calculate variance */
        double var_sum = 0.0;
        for (uint32_t i = 0; i < pps_offset_history_count; i++) {
            double diff = pps_offset_history[i] - mean;
            var_sum += diff * diff;
        }
        pps_offset_stddev = sqrt(var_sum / pps_offset_history_count);
    }
}

/* Debug: count captures to verify PIO is working */
static uint32_t fe_pps_debug_count = 0;
static uint32_t gps_pps_debug_count = 0;

/**
 * Poll PPS capture FIFOs - call from main loop
 * Both PIO SMs capture 10MHz count at their respective PPS edges
 * and push to FIFO. We poll both FIFOs and update stats when we have both.
 */
void freq_counter_pps_task(void) {
    uint32_t count;

    /* Poll Rb PPS capture FIFO */
    if (pps_capture_read(freq_pio, fe_pps_sm, &count)) {
        fe_pps_capture_count = count;
        fe_pps_capture_valid = true;
        fe_pps_debug_count++;
        if (fe_pps_debug_count <= 5) {
            printf("[FREQ] Rb PPS capture #%lu: %lu\n",
                   (unsigned long)fe_pps_debug_count, (unsigned long)count);
        }
    }

    /* Poll GPS PPS capture FIFO */
    if (pps_capture_read(freq_pio, gps_pps_sm, &count)) {
        gps_pps_capture_count = count;
        gps_pps_capture_valid = true;
        gps_pps_debug_count++;
        if (gps_pps_debug_count <= 5) {
            printf("[FREQ] GPS PPS capture #%lu: %lu\n",
                   (unsigned long)gps_pps_debug_count, (unsigned long)count);
        }

        /* Update statistics when we get a new GPS PPS capture */
        if (fe_pps_capture_valid) {
            update_pps_offset_stats();
        }
    }
}

/**
 * Legacy: Capture GPS PPS from IRQ handler
 * Now a no-op - PIO handles capture automatically
 */
void freq_counter_capture_gps_pps(void) {
    /* No-op: PIO SM2 captures automatically at GPS PPS edge */
}

/**
 * Get PPS offset in 10MHz ticks (100ns resolution)
 */
int32_t freq_counter_get_pps_offset(void) {
    if (!fe_pps_capture_valid || !gps_pps_capture_valid) {
        return 0;
    }
    return pps_offset_last;
}

/**
 * Get PPS offset drift rate in ticks per second
 * Positive = GPS drifting later relative to FE
 * Negative = GPS drifting earlier relative to FE
 */
double freq_counter_get_pps_drift(void) {
    return pps_drift_rate;
}

/**
 * Get PPS offset standard deviation in ticks
 */
double freq_counter_get_pps_stddev(void) {
    return pps_offset_stddev;
}

/**
 * Check if PPS offset measurement is valid
 */
bool freq_counter_pps_offset_valid(void) {
    return fe_pps_capture_valid && gps_pps_capture_valid && (pps_offset_history_count > 0);
}

/**
 * Get debug counters for diagnostics
 */
uint32_t freq_counter_get_fe_pps_count(void) {
    return fe_pps_debug_count;
}

uint32_t freq_counter_get_gps_pps_count(void) {
    return gps_pps_debug_count;
}

/**
 * Get capture valid flags
 */
bool freq_counter_fe_pps_valid(void) {
    return fe_pps_capture_valid;
}

bool freq_counter_gps_pps_valid(void) {
    return gps_pps_capture_valid;
}
