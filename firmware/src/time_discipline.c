/**
 * CHRONOS-Rb Time Discipline Module
 * 
 * Implements a PI (Proportional-Integral) controller to discipline the
 * system time to the rubidium 1PPS reference.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"

#include "chronos_rb.h"

/*============================================================================
 * DISCIPLINE PARAMETERS
 *============================================================================*/

/* PI controller gains - tuned for rubidium stability */
static double kp = DISCIPLINE_GAIN_P;  /* Proportional gain */
static double ki = DISCIPLINE_GAIN_I;  /* Integral gain */

/* Time constants */
static uint32_t tau = DISCIPLINE_TAU_FAST;  /* Current time constant */

/* Controller state */
static double integral_term = 0.0;
static double last_offset_ns = 0.0;
static double frequency_correction = 0.0;  /* ppb */

/* Offset statistics */
#define OFFSET_HISTORY_SIZE 128
static int64_t offset_history[OFFSET_HISTORY_SIZE];
static uint32_t offset_history_index = 0;
static uint32_t offset_history_count = 0;

/* Allan deviation calculation */
static double allan_dev_1s = 0.0;
static double allan_dev_10s = 0.0;
static double allan_dev_100s = 0.0;

/* State tracking */
static uint32_t discipline_updates = 0;
static uint32_t lock_count = 0;
static bool is_locked = false;
static uint64_t last_update_time = 0;

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize the time discipline loop
 */
void discipline_init(void) {
    printf("[DISC] Initializing time discipline loop\n");
    printf("[DISC] Kp=%.3f, Ki=%.3f, Tau=%lu\n", kp, ki, tau);
    
    /* Clear state */
    integral_term = 0.0;
    last_offset_ns = 0.0;
    frequency_correction = 0.0;
    discipline_updates = 0;
    lock_count = 0;
    is_locked = false;
    
    /* Clear history */
    memset(offset_history, 0, sizeof(offset_history));
    offset_history_index = 0;
    offset_history_count = 0;
    
    last_update_time = time_us_64();
}

/*============================================================================
 * DISCIPLINE UPDATE
 *============================================================================*/

/**
 * Update the discipline loop with a new offset measurement
 * @param offset_ns Time offset from reference in nanoseconds
 *                  Positive = our clock is ahead, Negative = behind
 */
void discipline_update(int64_t offset_ns) {
    uint64_t now = time_us_64();
    double dt = (now - last_update_time) / 1e6;  /* Delta time in seconds */
    last_update_time = now;
    
    /* Sanity check */
    if (dt <= 0.0 || dt > 10.0) {
        dt = 1.0;  /* Default to 1 second */
    }
    
    /* Store in history */
    offset_history[offset_history_index] = offset_ns;
    offset_history_index = (offset_history_index + 1) % OFFSET_HISTORY_SIZE;
    if (offset_history_count < OFFSET_HISTORY_SIZE) {
        offset_history_count++;
    }
    
    /* Update statistics */
    g_time_state.offset_ns = offset_ns;
    if (offset_ns < g_stats.min_offset_ns || g_stats.min_offset_ns == 0) {
        g_stats.min_offset_ns = offset_ns;
    }
    if (offset_ns > g_stats.max_offset_ns) {
        g_stats.max_offset_ns = offset_ns;
    }
    
    /* Running average */
    double alpha = 0.01;
    g_stats.avg_offset_ns = alpha * offset_ns + (1.0 - alpha) * g_stats.avg_offset_ns;
    
    /* Calculate PI controller output */
    double offset_s = offset_ns / 1e9;  /* Convert to seconds */
    
    /* Proportional term */
    double p_term = kp * offset_s;
    
    /* Integral term with anti-windup */
    integral_term += ki * offset_s * dt;
    
    /* Limit integral term to prevent windup */
    double max_integral = 100e-9;  /* 100 ppb max integral contribution */
    if (integral_term > max_integral) {
        integral_term = max_integral;
    } else if (integral_term < -max_integral) {
        integral_term = -max_integral;
    }
    
    /* Combined correction in seconds per second (frequency offset) */
    double correction = p_term + integral_term;
    
    /* Convert to ppb */
    frequency_correction = correction * 1e9;
    
    /* Update global state */
    g_time_state.frequency_offset = frequency_correction;
    
    /* Check for lock */
    double abs_offset = fabs((double)offset_ns);
    if (abs_offset < 1000.0) {  /* Within 1 microsecond */
        lock_count++;
        if (lock_count > 60) {  /* Locked for 60 consecutive updates */
            if (!is_locked) {
                printf("[DISC] Time discipline LOCKED (offset < 1µs)\n");
                is_locked = true;
                
                /* Switch to slow time constant for stability */
                tau = DISCIPLINE_TAU_SLOW;
                kp = DISCIPLINE_GAIN_P * 0.5;
                ki = DISCIPLINE_GAIN_I * 0.5;
            }
        }
    } else if (abs_offset > 10000.0) {  /* More than 10 microseconds */
        if (is_locked) {
            printf("[DISC] Lost lock (offset = %lld ns)\n", offset_ns);
            is_locked = false;
            
            /* Switch back to fast time constant */
            tau = DISCIPLINE_TAU_FAST;
            kp = DISCIPLINE_GAIN_P;
            ki = DISCIPLINE_GAIN_I;
        }
        lock_count = 0;
    }
    
    discipline_updates++;
    last_offset_ns = (double)offset_ns;
    
    /* Debug output every 10 updates */
    if (discipline_updates % 10 == 0) {
        printf("[DISC] Update %lu: offset=%lld ns, correction=%.3f ppb, locked=%s\n",
               discipline_updates, offset_ns, frequency_correction,
               is_locked ? "YES" : "NO");
    }
}

/**
 * Get the current frequency correction in ppb
 */
double discipline_get_correction(void) {
    return frequency_correction;
}

/**
 * Check if the discipline loop is locked
 */
bool discipline_is_locked(void) {
    return is_locked;
}

/**
 * Get the number of discipline updates
 */
uint32_t discipline_get_update_count(void) {
    return discipline_updates;
}

/**
 * Reset the discipline loop (e.g., after a large step)
 */
void discipline_reset(void) {
    printf("[DISC] Resetting discipline loop\n");
    integral_term = 0.0;
    frequency_correction = 0.0;
    lock_count = 0;
    is_locked = false;
    tau = DISCIPLINE_TAU_FAST;
    kp = DISCIPLINE_GAIN_P;
    ki = DISCIPLINE_GAIN_I;
}

/**
 * Apply a time step (for initial synchronization)
 * @param step_ns Step to apply in nanoseconds
 */
void discipline_apply_step(int64_t step_ns) {
    printf("[DISC] Applying time step of %lld ns\n", step_ns);
    
    /* Clear integral term after a step */
    integral_term = 0.0;
    lock_count = 0;
}

/*============================================================================
 * STATISTICS
 *============================================================================*/

/**
 * Calculate Allan deviation at specified tau
 * @param tau_samples Number of samples for tau (1, 10, 100 seconds)
 * @return Allan deviation in seconds
 */
double calculate_allan_deviation(uint32_t tau_samples) {
    if (offset_history_count < tau_samples * 3) {
        return -1.0;  /* Not enough data */
    }
    
    /* Calculate Allan variance */
    double sum = 0.0;
    int count = 0;
    
    for (uint32_t i = 0; i < offset_history_count - 2 * tau_samples; i++) {
        double y1 = offset_history[i] / 1e9;
        double y2 = offset_history[i + tau_samples] / 1e9;
        double y3 = offset_history[i + 2 * tau_samples] / 1e9;
        
        double diff = y3 - 2 * y2 + y1;
        sum += diff * diff;
        count++;
    }
    
    if (count == 0) {
        return -1.0;
    }
    
    double allan_var = sum / (2.0 * count);
    return sqrt(allan_var);
}

/**
 * Update Allan deviation calculations
 */
void discipline_update_allan(void) {
    allan_dev_1s = calculate_allan_deviation(1);
    allan_dev_10s = calculate_allan_deviation(10);
    allan_dev_100s = calculate_allan_deviation(100);
}

/**
 * Get Allan deviation at 1 second
 */
double get_allan_dev_1s(void) {
    return allan_dev_1s;
}

/**
 * Get current offset in nanoseconds
 */
int64_t discipline_get_offset_ns(void) {
    return (int64_t)last_offset_ns;
}

/**
 * Get the integral term (for debugging)
 */
double discipline_get_integral(void) {
    return integral_term;
}

/**
 * Set discipline gains (for tuning)
 */
void discipline_set_gains(double new_kp, double new_ki) {
    kp = new_kp;
    ki = new_ki;
    printf("[DISC] Gains updated: Kp=%.3f, Ki=%.3f\n", kp, ki);
}

/**
 * Get current discipline gains
 */
void discipline_get_gains(double *out_kp, double *out_ki) {
    *out_kp = kp;
    *out_ki = ki;
}
