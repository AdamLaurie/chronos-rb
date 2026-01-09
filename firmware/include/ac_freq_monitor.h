/**
 * CHRONOS-Rb AC Mains Frequency Monitor
 *
 * Measures local AC mains frequency from zero-crossing detector input.
 * The zero-crossing detector (H11AA1 or similar) produces a pulse at
 * each AC zero crossing, resulting in 2x the mains frequency (100Hz for
 * 50Hz mains, 120Hz for 60Hz mains).
 *
 * Typical mains frequency ranges:
 *   - 50 Hz regions: 49.5 - 50.5 Hz nominal (Europe, Asia, Africa, Australia)
 *   - 60 Hz regions: 59.5 - 60.5 Hz nominal (Americas, parts of Asia)
 *
 * Grid frequency is tightly regulated and provides an interesting
 * indicator of grid load and stability.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef AC_FREQ_MONITOR_H
#define AC_FREQ_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* Measurement parameters */
#define AC_FREQ_SAMPLE_WINDOW_MS    1000    /* Averaging window in ms */
#define AC_FREQ_MIN_HZ              45.0f   /* Minimum valid frequency */
#define AC_FREQ_MAX_HZ              65.0f   /* Maximum valid frequency */
#define AC_FREQ_TIMEOUT_MS          100     /* Timeout for signal loss */

/* History buffer sizes */
#define AC_FREQ_HISTORY_SIZE        60      /* Short-term samples for instant average */
#define AC_FREQ_MINUTE_HISTORY      60      /* Last 60 minutes at minute resolution */
#define AC_FREQ_HOUR_HISTORY        48      /* Last 48 hours at hour resolution */

/*============================================================================
 * DATA STRUCTURES
 *============================================================================*/

typedef struct {
    float frequency_hz;             /* Current measured frequency (Hz) */
    float frequency_avg_hz;         /* Averaged frequency (Hz) */
    float frequency_min_hz;         /* Minimum observed frequency */
    float frequency_max_hz;         /* Maximum observed frequency */
    uint32_t zero_cross_count;      /* Total zero-crossing count */
    uint32_t last_edge_time_us;     /* Timestamp of last zero crossing */
    uint32_t period_us;             /* Measured period (us) */
    bool signal_present;            /* Zero-crossing signal detected */
    bool frequency_valid;           /* Frequency within valid range */
} ac_freq_state_t;

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize AC frequency monitor
 * Sets up GPIO and interrupt for zero-crossing detection
 */
void ac_freq_init(void);

/**
 * AC zero-crossing IRQ handler
 * Called from shared GPIO callback in gps_input.c
 */
void ac_zero_cross_irq_handler(void);

/**
 * Process AC frequency measurements
 * Call from main loop to update averaging
 */
void ac_freq_task(void);

/**
 * Get current AC mains frequency
 * @return Frequency in Hz, or 0.0 if not valid
 */
float ac_freq_get_hz(void);

/**
 * Get averaged AC mains frequency
 * @return Averaged frequency in Hz over the sample window
 */
float ac_freq_get_avg_hz(void);

/**
 * Check if AC frequency measurement is valid
 * @return true if signal present and frequency in valid range
 */
bool ac_freq_is_valid(void);

/**
 * Check if zero-crossing signal is present
 * @return true if pulses detected within timeout period
 */
bool ac_freq_signal_present(void);

/**
 * Get full AC frequency state
 * @return Pointer to internal state structure
 */
const ac_freq_state_t* ac_freq_get_state(void);

/**
 * Print AC frequency status to console
 */
void ac_freq_print_status(void);

/**
 * Get minute history buffer
 * @param buf Output buffer for minute averages (oldest first)
 * @param max_samples Maximum samples to return
 * @return Number of valid samples copied
 */
int ac_freq_get_minute_history(float *buf, int max_samples);

/**
 * Get hour history buffer
 * @param buf Output buffer for hour averages (oldest first)
 * @param max_samples Maximum samples to return
 * @return Number of valid samples copied
 */
int ac_freq_get_hour_history(float *buf, int max_samples);

/**
 * Get current accumulator status for diagnostics
 */
void ac_freq_get_accum_status(uint32_t *sec_count, uint32_t *min_count);

#endif /* AC_FREQ_MONITOR_H */
