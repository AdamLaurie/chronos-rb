/**
 * CHRONOS-Rb Radio Timecode Simulator
 *
 * Generates DCF77, WWVB, and JJY radio time signals
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef RADIO_TIMECODE_H
#define RADIO_TIMECODE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Radio signal types
 */
typedef enum {
    RADIO_DCF77,    /* Germany 77.5kHz */
    RADIO_WWVB,     /* USA 60kHz */
    RADIO_JJY40,    /* Japan 40kHz (Fukushima) */
    RADIO_JJY60     /* Japan 60kHz (Kyushu) */
} radio_signal_t;

/**
 * Initialize radio timecode outputs
 * DCF77: GP2 (77.5kHz)
 * WWVB:  GP3 (60kHz)
 * JJY40: GP4 (40kHz)
 * JJY60: GP26 (60kHz)
 */
void radio_timecode_init(void);

/**
 * Radio timecode task - call frequently from main loop
 */
void radio_timecode_task(void);

/**
 * Enable/disable individual signal outputs
 */
void radio_timecode_enable(radio_signal_t signal, bool enable);

/**
 * Check if a signal is enabled
 */
bool radio_timecode_is_enabled(radio_signal_t signal);

/**
 * Get GPIO pin for a signal
 */
uint8_t radio_timecode_get_gpio(radio_signal_t signal);

#endif /* RADIO_TIMECODE_H */
