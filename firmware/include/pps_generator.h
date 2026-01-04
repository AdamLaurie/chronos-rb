/**
 * CHRONOS-Rb PPS Generator Module
 *
 * Generates 1PPS (1 pulse per second) from 10MHz input using PIO.
 * Divides 10MHz by 10,000,000 to produce accurate 1Hz output.
 *
 * The generated 1PPS is output on GPIO_DEBUG_PPS_OUT (GP10) and should
 * be wired to GPIO_PPS_INPUT (GP2) for the PPS capture module to use.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef PPS_GENERATOR_H
#define PPS_GENERATOR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize the PPS generator
 *
 * Sets up PIO to count 10MHz cycles and generate 1PPS output.
 * Does not start generation - call pps_generator_start() to begin.
 */
void pps_generator_init(void);

/**
 * Start PPS generation
 *
 * Begins counting 10MHz cycles and generating 1PPS output.
 * The first pulse will occur 1 second after the 10MHz signal is detected.
 */
void pps_generator_start(void);

/**
 * Stop PPS generation
 */
void pps_generator_stop(void);

/**
 * Check if PPS generator is running
 */
bool pps_generator_is_running(void);

/**
 * Get the number of 1PPS pulses generated
 */
uint32_t pps_generator_get_count(void);

/**
 * Set the source mode for 1PPS
 *
 * @param use_internal If true, use internally generated 1PPS from 10MHz
 *                     If false, expect external 1PPS on GPIO_PPS_INPUT
 */
void pps_set_internal_source(bool use_internal);

/**
 * Check if using internal PPS source
 */
bool pps_is_internal_source(void);

#endif /* PPS_GENERATOR_H */
