/**
 * CHRONOS-Rb PPS Capture Module
 * 
 * Captures the 1PPS signal from the FE-5680A rubidium oscillator with
 * sub-microsecond precision using the RP2350's PIO.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "chronos_rb.h"
#include "pps_capture.pio.h"

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static PIO pps_pio = pio0;
static uint pps_sm = 0;

/* Timestamp storage */
static volatile uint64_t pps_timestamp_us = 0;
static volatile uint32_t pps_timestamp_cycles = 0;
static volatile uint64_t prev_pps_timestamp_us = 0;
static volatile uint32_t pps_edge_count = 0;

/* PPS quality metrics */
static volatile int32_t pps_period_error_ns = 0;
static volatile uint32_t pps_valid_count = 0;
static volatile uint32_t pps_invalid_count = 0;

/* Circular buffer for PPS timestamps (for jitter analysis) */
#define PPS_HISTORY_SIZE 64
static volatile uint64_t pps_history[PPS_HISTORY_SIZE];
static volatile uint32_t pps_history_index = 0;

/*============================================================================
 * IRQ HANDLER
 *============================================================================*/

/**
 * PIO IRQ handler - called on rising edge of 1PPS
 * 
 * This is time-critical code. We capture the timestamp as quickly as
 * possible, then do any processing afterward.
 */
static void pps_pio_irq_handler(void) {
    /* Read timestamp immediately - this is the critical path */
    uint64_t now_us = time_us_64();
    uint32_t now_cycles = timer_hw->timerawl;  /* Lower 32 bits of timer */
    
    /* Clear the IRQ */
    pio_interrupt_clear(pps_pio, 0);
    
    /* Calculate period from previous pulse */
    uint64_t period_us = now_us - prev_pps_timestamp_us;
    
    /* Validate the pulse */
    bool valid = false;
    if (pps_edge_count > 0) {
        /* Check if period is within tolerance (1 second ± tolerance) */
        if (period_us >= (PPS_NOMINAL_PERIOD_US - PPS_TOLERANCE_US) &&
            period_us <= (PPS_NOMINAL_PERIOD_US + PPS_TOLERANCE_US)) {
            valid = true;
            pps_valid_count++;
            
            /* Calculate period error in nanoseconds */
            pps_period_error_ns = (int32_t)(period_us - PPS_NOMINAL_PERIOD_US) * 1000;
        } else {
            pps_invalid_count++;
            printf("[PPS] Invalid period: %llu us (expected ~%lu us)\n", 
                   period_us, PPS_NOMINAL_PERIOD_US);
        }
    }
    
    /* Store timestamp */
    prev_pps_timestamp_us = pps_timestamp_us;
    pps_timestamp_us = now_us;
    pps_timestamp_cycles = now_cycles;
    pps_edge_count++;
    
    /* Store in history buffer */
    pps_history[pps_history_index] = now_us;
    pps_history_index = (pps_history_index + 1) % PPS_HISTORY_SIZE;
    
    /* Update global state */
    g_time_state.pps_count = pps_edge_count;
    
    /* Toggle debug output for scope monitoring */
    gpio_put(GPIO_DEBUG_PPS_OUT, 1);
    
    /* Call the sync handler if we have a valid pulse */
    if (valid) {
        pps_irq_handler();
    }
}

/**
 * GPIO IRQ handler (fallback if PIO not available)
 */
static void pps_gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == GPIO_PPS_INPUT && (events & GPIO_IRQ_EDGE_RISE)) {
        pps_pio_irq_handler();
    }
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize PPS capture using PIO
 */
void pps_capture_init(void) {
    printf("[PPS] Initializing PPS capture on GPIO %d\n", GPIO_PPS_INPUT);
    
    /* Try to use PIO for precise capture */
    uint offset = pio_add_program(pps_pio, &pps_capture_program);
    
    /* Initialize the PIO program */
    pps_capture_program_init(pps_pio, pps_sm, offset, 
                             GPIO_PPS_INPUT, GPIO_DEBUG_PPS_OUT);
    
    /* Configure PIO IRQ */
    pio_set_irq0_source_enabled(pps_pio, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pps_pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    
    /* Start the state machine */
    pio_sm_set_enabled(pps_pio, pps_sm, true);
    
    /* Clear history buffer */
    for (int i = 0; i < PPS_HISTORY_SIZE; i++) {
        pps_history[i] = 0;
    }
    
    printf("[PPS] PIO capture initialized, SM %d at offset %d\n", pps_sm, offset);
    printf("[PPS] Waiting for first PPS pulse...\n");
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Get the timestamp of the last PPS pulse in microseconds
 */
uint64_t get_last_pps_timestamp(void) {
    uint32_t irq = save_and_disable_interrupts();
    uint64_t ts = pps_timestamp_us;
    restore_interrupts(irq);
    return ts;
}

/**
 * Get the timestamp of the last PPS pulse with cycle precision
 */
void get_last_pps_timestamp_precise(uint64_t *us, uint32_t *cycles) {
    uint32_t irq = save_and_disable_interrupts();
    *us = pps_timestamp_us;
    *cycles = pps_timestamp_cycles;
    restore_interrupts(irq);
}

/**
 * Get the number of PPS pulses received
 */
uint32_t get_pps_count(void) {
    return pps_edge_count;
}

/**
 * Get the last measured PPS period error in nanoseconds
 */
int32_t get_pps_period_error_ns(void) {
    return pps_period_error_ns;
}

/**
 * Get PPS quality statistics
 */
void get_pps_statistics(uint32_t *valid, uint32_t *invalid) {
    *valid = pps_valid_count;
    *invalid = pps_invalid_count;
}

/**
 * Calculate PPS jitter (standard deviation of period)
 * Returns jitter in nanoseconds
 */
int32_t calculate_pps_jitter_ns(void) {
    if (pps_edge_count < 3) {
        return -1;  /* Not enough data */
    }
    
    uint32_t irq = save_and_disable_interrupts();
    
    /* Calculate periods from history */
    int64_t periods[PPS_HISTORY_SIZE - 1];
    int count = 0;
    
    for (int i = 1; i < PPS_HISTORY_SIZE && i < pps_edge_count; i++) {
        uint32_t idx = (pps_history_index - i + PPS_HISTORY_SIZE) % PPS_HISTORY_SIZE;
        uint32_t prev_idx = (idx - 1 + PPS_HISTORY_SIZE) % PPS_HISTORY_SIZE;
        
        if (pps_history[idx] > 0 && pps_history[prev_idx] > 0) {
            periods[count++] = (int64_t)(pps_history[idx] - pps_history[prev_idx]);
        }
    }
    
    restore_interrupts(irq);
    
    if (count < 2) {
        return -1;
    }
    
    /* Calculate mean */
    int64_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += periods[i];
    }
    int64_t mean = sum / count;
    
    /* Calculate variance */
    int64_t var_sum = 0;
    for (int i = 0; i < count; i++) {
        int64_t diff = periods[i] - mean;
        var_sum += diff * diff;
    }
    int64_t variance = var_sum / count;
    
    /* Return standard deviation in nanoseconds */
    /* Simple integer square root approximation */
    int64_t std_dev_us = 0;
    if (variance > 0) {
        int64_t x = variance;
        int64_t y = (x + 1) / 2;
        while (y < x) {
            x = y;
            y = (x + variance / x) / 2;
        }
        std_dev_us = x;
    }
    
    return (int32_t)(std_dev_us * 1000);  /* Convert µs to ns */
}

/**
 * Check if PPS signal is present and valid
 */
bool is_pps_valid(void) {
    /* Check if we've received a pulse in the last 2 seconds */
    uint64_t now = time_us_64();
    uint64_t last = get_last_pps_timestamp();
    
    if (last == 0) {
        return false;
    }
    
    uint64_t age = now - last;
    return (age < 2000000);  /* Less than 2 seconds old */
}

/**
 * Get time since last PPS pulse in microseconds
 */
uint64_t get_time_since_pps(void) {
    uint64_t now = time_us_64();
    uint64_t last = get_last_pps_timestamp();
    
    if (last == 0 || now < last) {
        return 0;
    }
    
    return now - last;
}
