/**
 * CHRONOS-Rb PPS Generator Module
 *
 * Generates 1PPS from 10MHz input by counting 10,000,000 cycles.
 * Uses PIO state machine for cycle-accurate counting.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

#include "chronos_rb.h"
#include "pps_generator.h"
#include "pps_generator.pio.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Use PIO1 SM1 (SM0 is used by frequency counter) */
static PIO gen_pio = pio1;
static uint gen_sm = 1;

/* State */
static volatile bool generator_running = false;
static volatile uint32_t generated_pps_count = 0;
static volatile bool use_internal_pps = true;

/* Rising edges for 1 second at 10MHz */
#define PPS_CYCLES_PER_SECOND   10000000UL

/*============================================================================
 * IRQ HANDLER
 *============================================================================*/

/**
 * PIO IRQ handler - called when 1PPS pulse is generated
 */
static void pps_generator_irq_handler(void) {
    /* Clear the IRQ */
    pio_interrupt_clear(gen_pio, 0);

    /* Increment counter */
    generated_pps_count++;

    /* Debug output - briefly flash the activity LED */
    gpio_put(GPIO_LED_ACTIVITY, 1);
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize the PPS generator
 */
void pps_generator_init(void) {
    printf("[PPS-GEN] Initializing 1PPS generator from 10MHz\n");
    printf("[PPS-GEN] Input: GPIO %d (10MHz), Output: GPIO %d (1PPS)\n",
           GPIO_10MHZ_INPUT, GPIO_DEBUG_PPS_OUT);

    /* Add PIO program */
    uint offset = pio_add_program(gen_pio, &pps_generator_program);

    /* Initialize the PIO state machine */
    pps_generator_program_init(gen_pio, gen_sm, offset,
                                GPIO_10MHZ_INPUT, GPIO_DEBUG_PPS_OUT);

    /* Configure PIO IRQ for pulse notification */
    pio_set_irq1_source_enabled(gen_pio, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO1_IRQ_1, pps_generator_irq_handler);
    irq_set_enabled(PIO1_IRQ_1, true);

    printf("[PPS-GEN] PIO initialized, SM %d at offset %d\n", gen_sm, offset);
    printf("[PPS-GEN] Wire GPIO %d to GPIO %d for PPS input\n",
           GPIO_DEBUG_PPS_OUT, GPIO_PPS_INPUT);
}

/**
 * Start PPS generation
 */
void pps_generator_start(void) {
    if (generator_running) {
        return;
    }

    printf("[PPS-GEN] Starting 1PPS generation (count=%lu, pushed=%lu)\n",
           PPS_CYCLES_PER_SECOND, PPS_CYCLES_PER_SECOND - 1);

    /* Drain any old data from FIFOs */
    while (!pio_sm_is_rx_fifo_empty(gen_pio, gen_sm)) {
        pio_sm_get(gen_pio, gen_sm);
    }

    /* Load the cycle count and enable the state machine */
    pps_generator_load_count(gen_pio, gen_sm, PPS_CYCLES_PER_SECOND);
    pio_sm_set_enabled(gen_pio, gen_sm, true);

    generator_running = true;
    printf("[PPS-GEN] Generator started - first pulse in ~1 second\n");
}

/**
 * Stop PPS generation
 */
void pps_generator_stop(void) {
    if (!generator_running) {
        return;
    }

    printf("[PPS-GEN] Stopping generator\n");

    /* Disable state machine */
    pio_sm_set_enabled(gen_pio, gen_sm, false);

    /* Clear output */
    gpio_put(GPIO_DEBUG_PPS_OUT, 0);

    generator_running = false;
}

/**
 * Check if generator is running
 */
bool pps_generator_is_running(void) {
    return generator_running;
}

/**
 * Get number of generated pulses
 */
uint32_t pps_generator_get_count(void) {
    return generated_pps_count;
}

/**
 * Set the PPS source mode
 */
void pps_set_internal_source(bool use_internal) {
    use_internal_pps = use_internal;
    if (use_internal) {
        printf("[PPS-GEN] Using internal 1PPS (generated from 10MHz)\n");
    } else {
        printf("[PPS-GEN] Using external 1PPS source\n");
    }
}

/**
 * Check if using internal PPS
 */
bool pps_is_internal_source(void) {
    return use_internal_pps;
}
