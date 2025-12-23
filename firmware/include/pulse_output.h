/**
 * CHRONOS-Rb Configurable Pulse Output
 *
 * Configurable GPIO pulse outputs with interval and time-based triggers
 *
 * Commands:
 *   pulse <pin> P <interval_sec> <width_ms>                    - Interval pulse
 *   pulse <pin> S <second> <width_ms> <count> <gap_ms>         - Second-triggered
 *   pulse <pin> M <minute> <width_ms> <count> <gap_ms>         - Minute-triggered
 *   pulse <pin> H <HH:MM> <width_ms> <count> <gap_ms>          - Time-triggered
 *   pulse <pin> off                                            - Disable output
 *   pulse list                                                 - List configurations
 *   pulse clear                                                - Clear all
 *
 * For time-triggered modes (S/M/H):
 *   count  = number of pulses to generate in burst (1 = single pulse)
 *   gap_ms = gap between pulses in burst (milliseconds)
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef PULSE_OUTPUT_H
#define PULSE_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define MAX_PULSE_OUTPUTS   8

/*============================================================================
 * PULSE TRIGGER TYPES
 *============================================================================*/

typedef enum {
    PULSE_MODE_DISABLED = 0,    /* Output disabled */
    PULSE_MODE_INTERVAL,        /* Pulse every N seconds */
    PULSE_MODE_SECOND,          /* Pulse on specific second (0-59) each minute */
    PULSE_MODE_MINUTE,          /* Pulse on specific minute (0-59) each hour */
    PULSE_MODE_TIME             /* Pulse at specific time (HH:MM) each day */
} pulse_mode_t;

/*============================================================================
 * PULSE CONFIGURATION
 *============================================================================*/

typedef struct {
    uint8_t gpio_pin;           /* GPIO pin number */
    pulse_mode_t mode;          /* Trigger mode */
    uint32_t interval;          /* Interval in seconds (for INTERVAL mode) */
    uint8_t trigger_second;     /* Second to trigger (0-59) */
    uint8_t trigger_minute;     /* Minute to trigger (0-59) */
    uint8_t trigger_hour;       /* Hour to trigger (0-23) for TIME mode */
    uint16_t pulse_width_ms;    /* Pulse width in milliseconds */
    uint16_t pulse_count;       /* Number of pulses in burst */
    uint16_t pulse_gap_ms;      /* Gap between pulses in burst (ms) */
    bool active;                /* Configuration is active */

    /* Runtime state for burst generation */
    uint32_t last_trigger_pps;  /* PPS count at last trigger */
    uint32_t pulse_off_time;    /* Time to turn off current pulse (0 = off) */
    uint32_t next_pulse_time;   /* Time to start next pulse in burst (0 = none) */
    uint16_t burst_remaining;   /* Pulses remaining in current burst */
    bool triggered_this_period; /* Already triggered in current period */
} pulse_config_t;

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/* Initialize pulse output system */
void pulse_output_init(void);

/* Process pulse outputs - call from main loop */
void pulse_output_task(void);

/* Configure interval-based pulse
 * Returns slot index (0-7) on success, -1 on failure */
int pulse_output_set_interval(uint8_t gpio_pin, uint32_t interval_sec,
                              uint16_t pulse_width_ms);

/* Configure second-triggered pulse (fires on specific second each minute)
 * count: number of pulses in burst, gap_ms: gap between pulses */
int pulse_output_set_second(uint8_t gpio_pin, uint8_t second,
                            uint16_t pulse_width_ms, uint16_t count,
                            uint16_t gap_ms);

/* Configure minute-triggered pulse (fires on specific minute each hour)
 * count: number of pulses in burst, gap_ms: gap between pulses */
int pulse_output_set_minute(uint8_t gpio_pin, uint8_t minute,
                            uint16_t pulse_width_ms, uint16_t count,
                            uint16_t gap_ms);

/* Configure time-triggered pulse (fires at specific HH:MM each day)
 * count: number of pulses in burst, gap_ms: gap between pulses */
int pulse_output_set_time(uint8_t gpio_pin, uint8_t hour, uint8_t minute,
                          uint16_t pulse_width_ms, uint16_t count,
                          uint16_t gap_ms);

/* Disable a pulse output */
bool pulse_output_disable(uint8_t gpio_pin);

/* List all pulse configurations */
void pulse_output_list(void);

/* Get pulse configuration by index */
pulse_config_t* pulse_output_get(int index);

/* Clear all pulse configurations */
void pulse_output_clear_all(void);

#endif /* PULSE_OUTPUT_H */
