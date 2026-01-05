/**
 * CHRONOS-Rb IRIG-B Timecode Output
 *
 * Generates IRIG-B timecode for aerospace/military/test equipment
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef IRIG_B_H
#define IRIG_B_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize IRIG-B output on GP27
 */
void irig_b_init(void);

/**
 * IRIG-B task - call frequently from main loop
 */
void irig_b_task(void);

/**
 * Enable/disable IRIG-B output
 */
void irig_b_enable(bool enable);

/**
 * Set IRIG-B mode
 * @param modulated  true = IRIG-B120 (1kHz AM), false = IRIG-B000 (DC level shift)
 */
void irig_b_set_mode(bool modulated);

/**
 * Check if IRIG-B is enabled
 */
bool irig_b_is_enabled(void);

/**
 * Check if IRIG-B is in modulated mode
 */
bool irig_b_is_modulated(void);

#endif /* IRIG_B_H */
