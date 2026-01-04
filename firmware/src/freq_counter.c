/**
 * CHRONOS-Rb Frequency Counter Module
 * 
 * Measures the 10MHz reference frequency from the FE-5680A rubidium oscillator.
 * Uses PIO-based reciprocal counting for high precision.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"

#include "chronos_rb.h"
#include "freq_counter.pio.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Use PIO1 for frequency counter (PIO0 used for PPS) */
static PIO freq_pio = pio1;
static uint freq_sm = 0;
static uint freq_dma_chan = 0;

/* Measurement state */
static volatile uint32_t last_count = 0;
static volatile uint32_t measurement_count = 0;
static volatile bool measurement_ready = false;
static volatile uint64_t measurement_start_time = 0;
static volatile uint64_t measurement_end_time = 0;

/* Frequency offset tracking */
static double freq_offset_ppb = 0.0;
static double freq_offset_filtered = 0.0;
#define FREQ_FILTER_ALPHA 0.1  /* Low-pass filter coefficient */

/* Calibration */
static double system_clock_correction = 1.0;  /* Correction for system clock error */

/*============================================================================
 * PPS-GATED MEASUREMENT
 *============================================================================*/

/**
 * Read frequency count on PPS pulse
 * Called from PPS IRQ handler
 * PIO counts continuously, we just read and reset on each PPS
 */
void freq_counter_pps_start(void) {
    /* Read count since last PPS and reset */
    uint32_t count = freq_counter_read_and_reset(freq_pio, freq_sm);

    /* First reading after init will be invalid, skip it */
    static bool first_reading = true;
    if (first_reading) {
        first_reading = false;
        return;
    }

    last_count = count;
    measurement_ready = true;
    measurement_count++;
    g_time_state.last_freq_count = count;
    g_stats.freq_measurements++;
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize the frequency counter
 */
void freq_counter_init(void) {
    printf("[FREQ] Initializing frequency counter on GPIO %d\n", GPIO_10MHZ_INPUT);
    
    /* Add PIO program */
    uint offset = pio_add_program(freq_pio, &freq_counter_program);
    
    /* Initialize PIO state machine */
    freq_counter_program_init(freq_pio, freq_sm, offset, GPIO_10MHZ_INPUT);
    
    /* Start the state machine */
    pio_sm_set_enabled(freq_pio, freq_sm, true);
    
    printf("[FREQ] PIO counter initialized, SM %d at offset %d\n", freq_sm, offset);
    printf("[FREQ] Expected count per second: %lu\n", REF_CLOCK_HZ);
}

/*============================================================================
 * MEASUREMENT FUNCTIONS
 *============================================================================*/

/**
 * Start a timed frequency measurement
 * @param gate_time_ms Gate time in milliseconds
 */
void freq_counter_start_measurement(uint32_t gate_time_ms) {
    /* Calculate gate count based on system clock */
    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t gate_cycles = (sys_clk / 1000) * gate_time_ms;
    
    /* Apply system clock correction if calibrated */
    gate_cycles = (uint32_t)(gate_cycles * system_clock_correction);
    
    measurement_start_time = time_us_64();
    measurement_ready = false;
    
    /* Send gate count to PIO */
    pio_sm_put(freq_pio, freq_sm, gate_cycles);
}

/**
 * Check if a measurement is complete
 */
bool freq_counter_measurement_ready(void) {
    if (!measurement_ready && !pio_sm_is_rx_fifo_empty(freq_pio, freq_sm)) {
        last_count = pio_sm_get(freq_pio, freq_sm);
        measurement_end_time = time_us_64();
        measurement_ready = true;
        measurement_count++;
        g_time_state.last_freq_count = last_count;
        g_stats.freq_measurements++;
    }
    return measurement_ready;
}

/**
 * Get the last frequency count
 */
uint32_t freq_counter_read(void) {
    return last_count;
}

/**
 * Calculate frequency in Hz from last measurement
 */
double freq_counter_get_frequency(void) {
    if (!measurement_ready || measurement_end_time <= measurement_start_time) {
        return 0.0;
    }
    
    double gate_time_s = (measurement_end_time - measurement_start_time) / 1e6;
    return (double)last_count / gate_time_s;
}

/**
 * Get frequency offset in parts per billion (ppb)
 * Positive = faster than nominal, Negative = slower
 */
double get_frequency_offset_ppb(void) {
    if (!measurement_ready) {
        return freq_offset_filtered;
    }
    
    /* Calculate offset from nominal */
    double measured = freq_counter_get_frequency();
    double nominal = (double)REF_CLOCK_HZ;
    
    if (measured <= 0.0) {
        return 0.0;
    }
    
    freq_offset_ppb = ((measured - nominal) / nominal) * 1e9;
    
    /* Apply low-pass filter */
    freq_offset_filtered = (FREQ_FILTER_ALPHA * freq_offset_ppb) + 
                           ((1.0 - FREQ_FILTER_ALPHA) * freq_offset_filtered);
    
    g_time_state.frequency_offset = freq_offset_filtered;
    
    return freq_offset_filtered;
}

/**
 * Get raw frequency offset (unfiltered)
 */
double get_frequency_offset_ppb_raw(void) {
    return freq_offset_ppb;
}

/**
 * Calibrate the system clock using the rubidium reference
 * This should be called after the rubidium is locked and stable
 */
void freq_counter_calibrate_sysclk(void) {
    printf("[FREQ] Calibrating system clock against Rb reference...\n");
    
    /* Take multiple measurements and average */
    double sum = 0.0;
    int valid_count = 0;
    
    for (int i = 0; i < 10; i++) {
        freq_counter_start_measurement(1000);  /* 1 second gate */
        
        /* Wait for measurement */
        while (!freq_counter_measurement_ready()) {
            sleep_ms(10);
        }
        
        double freq = freq_counter_get_frequency();
        if (freq > 9000000 && freq < 11000000) {  /* Sanity check */
            sum += freq;
            valid_count++;
            printf("[FREQ] Sample %d: %.3f Hz\n", i + 1, freq);
        }
        
        sleep_ms(100);
    }
    
    if (valid_count >= 5) {
        double avg_freq = sum / valid_count;
        system_clock_correction = (double)REF_CLOCK_HZ / avg_freq;
        printf("[FREQ] System clock correction factor: %.9f\n", system_clock_correction);
        printf("[FREQ] Measured average: %.3f Hz, Nominal: %lu Hz\n", 
               avg_freq, REF_CLOCK_HZ);
    } else {
        printf("[FREQ] Calibration failed - insufficient valid samples\n");
    }
}

/**
 * Get the number of measurements taken
 */
uint32_t freq_counter_get_measurement_count(void) {
    return measurement_count;
}

/**
 * Check if the 10MHz signal is present
 */
bool freq_counter_signal_present(void) {
    /* Quick check - look for edges on the input */
    uint32_t start = time_us_32();
    int edges = 0;
    bool last_state = gpio_get(GPIO_10MHZ_INPUT);
    
    while (time_us_32() - start < 1000) {  /* Check for 1ms */
        bool state = gpio_get(GPIO_10MHZ_INPUT);
        if (state != last_state) {
            edges++;
            last_state = state;
            if (edges > 10) {
                return true;  /* Found signal */
            }
        }
    }
    
    return (edges > 5);  /* Need at least 5 edges in 1ms for 10MHz */
}

/**
 * Get the last measurement gate time in microseconds
 */
uint64_t freq_counter_get_gate_time_us(void) {
    if (measurement_end_time > measurement_start_time) {
        return measurement_end_time - measurement_start_time;
    }
    return 0;
}
