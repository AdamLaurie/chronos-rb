/**
 * CHRONOS-Rb Rubidium Synchronization Module
 * 
 * Manages synchronization to the FE-5680A rubidium oscillator,
 * handling the 10MHz reference and 1PPS signals.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/sync.h"

#include "chronos_rb.h"
#include "gps_input.h"

/* Forward declaration */
void set_time_unix(uint32_t unix_time);

/*============================================================================
 * STATE MACHINE
 *============================================================================*/

static sync_state_t current_state = SYNC_STATE_INIT;
static uint64_t state_enter_time = 0;
static uint32_t state_pps_count = 0;

/* Time tracking */
static uint32_t current_seconds = 0;     /* Seconds since startup (or epoch if set) */
static uint32_t subsecond_us = 0;        /* Microseconds within current second */
static uint64_t last_pps_us = 0;         /* Timestamp of last PPS */
static int64_t accumulated_offset = 0;   /* Accumulated time offset */

/* GPS time synchronization state */
static uint32_t pending_gps_time = 0;    /* GPS time to set on next PPS */
static bool gps_time_pending = false;    /* True if we have a GPS time waiting */

/* Rubidium status */
static bool rb_lock_status = false;
static uint32_t rb_lock_duration = 0;    /* Seconds since Rb locked */
static uint32_t rb_warmup_time = 0;      /* Warmup time in seconds */

/* Epoch configuration */
static bool epoch_set = false;
static uint32_t epoch_offset = 0;        /* Offset from Unix epoch */

/* NTP epoch: 1900-01-01 00:00:00
 * Unix epoch: 1970-01-01 00:00:00
 * Difference: 2208988800 seconds */
#define NTP_UNIX_OFFSET 2208988800UL

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize rubidium synchronization
 */
void rubidium_sync_init(void) {
    printf("[RB] Initializing rubidium synchronization\n");
    
    current_state = SYNC_STATE_INIT;
    state_enter_time = time_us_64();
    state_pps_count = 0;
    
    /* Initialize time to a default (will be set via NTP or manual) */
    current_seconds = 0;
    subsecond_us = 0;
    
    g_time_state.sync_state = current_state;
    g_time_state.time_valid = false;
    g_time_state.rb_locked = false;
    
    printf("[RB] Waiting for rubidium oscillator to lock...\n");
    printf("[RB] (FE-5680A typically needs 3-5 minutes warmup)\n");
}

/**
 * Change state machine state
 */
static void change_state(sync_state_t new_state) {
    const char *state_names[] = {
        "INIT", "FREQ_CAL", "COARSE", "FINE", "LOCKED", "HOLDOVER", "ERROR"
    };
    
    printf("[RB] State change: %s -> %s\n", 
           state_names[current_state], state_names[new_state]);
    
    current_state = new_state;
    state_enter_time = time_us_64();
    state_pps_count = 0;
    g_time_state.sync_state = new_state;
}

/*============================================================================
 * PPS INTERRUPT HANDLER
 *============================================================================*/

/**
 * Called from PPS capture IRQ on valid 1PPS edge
 */
void pps_irq_handler(void) {
    uint64_t pps_time = get_last_pps_timestamp();

    /* Note: freq_counter_pps_start() is called from pps_capture.c on every edge */

    state_pps_count++;

    /* Get offset from freq_counter which measures 10MHz cycles between PPS edges
     * This gives 100ns resolution from the rubidium reference itself.
     * freq_counter_get_error() = deviation from 10,000,000 cycles
     * Each count = 100ns */
    int64_t offset_ns = (int64_t)freq_counter_get_error() * 100LL;

    /* Update discipline loop with high-precision offset */
    discipline_update(offset_ns);

    /* Apply correction to subsecond counter */
    accumulated_offset += offset_ns;

    last_pps_us = pps_time;

    /* Apply pending GPS time if waiting
     * GPS NMEA arrives ~300ms after the PPS it refers to
     * So pending_gps_time is the time of the PREVIOUS GPS second
     * This PPS marks the start of the NEXT second */
    if (gps_time_pending) {
        current_seconds = pending_gps_time + 1;
        epoch_offset = 0;
        gps_time_pending = false;
        epoch_set = true;
    } else {
        /* Normal increment */
        current_seconds++;
    }
    subsecond_us = 0;

    /* Update global time state */
    g_time_state.current_time.seconds = current_seconds;
    g_time_state.current_time.fraction = 0;
}

/*============================================================================
 * STATE MACHINE TASK
 *============================================================================*/

/**
 * Check rubidium lock status
 */
static bool check_rb_lock(void) {
    /* FE-5680A lock: 0.8V=locked, 4.8V=unlocked
     * After NPN transistor level shifter: GPIO HIGH=locked, LOW=unlocked */
    bool locked = gpio_get(GPIO_RB_LOCK_STATUS);
    
    if (locked && !rb_lock_status) {
        printf("[RB] Rubidium oscillator LOCKED\n");
        rb_lock_duration = 0;
    } else if (!locked && rb_lock_status) {
        printf("[RB] WARNING: Rubidium oscillator UNLOCKED!\n");
    }
    
    rb_lock_status = locked;
    g_time_state.rb_locked = locked;
    
    return locked;
}

/**
 * Main synchronization task - call periodically
 */
void rubidium_sync_task(void) {
    static uint64_t last_task_time = 0;
    uint64_t now = time_us_64();
    
    /* Run at ~10Hz */
    if (now - last_task_time < 100000) {
        return;
    }
    last_task_time = now;
    
    /* Check rubidium lock status */
    bool rb_locked = check_rb_lock();
    
    /* Update subsecond counter */
    if (last_pps_us > 0) {
        subsecond_us = (now - last_pps_us) % 1000000;
        
        /* Apply frequency correction */
        double correction = discipline_get_correction();
        int64_t correction_ns = (int64_t)(subsecond_us * correction / 1e3);
        /* Correction is applied in timestamp generation */
    }
    
    /* Update warmup timer */
    static uint64_t last_warmup_time = 0;
    if (now - last_warmup_time >= 1000000) {
        rb_warmup_time++;
        if (rb_locked) {
            rb_lock_duration++;
        }
        last_warmup_time = now;
    }

    /* Queue GPS time if not already set (will be applied on next PPS edge)
     * GPS NMEA arrives ~300ms after the PPS pulse it describes
     * So we queue the time and apply it on the next PPS edge with +1 second */
    if (!epoch_set && !gps_time_pending && gps_has_time()) {
        uint32_t gps_time = gps_get_unix_time();
        if (gps_time > 0) {
            printf("[RB] Queueing GPS time %lu for next PPS edge\n", gps_time);
            pending_gps_time = gps_time;
            gps_time_pending = true;
        }
    }

    /* State machine */
    uint64_t state_time = (now - state_enter_time) / 1000000;  /* Seconds in state */
    
    switch (current_state) {
        case SYNC_STATE_INIT:
            /* Wait for rubidium to lock */
            if (rb_locked) {
                printf("[RB] Rubidium locked after %lu seconds warmup\n", rb_warmup_time);
                change_state(SYNC_STATE_FREQ_CAL);
            } else if (state_time > 600) {  /* 10 minute timeout */
                printf("[RB] ERROR: Rubidium failed to lock within 10 minutes\n");
                /* Check if GPS is available as fallback */
                if (gps_has_time() && gps_pps_valid()) {
                    printf("[RB] GPS available as fallback time source\n");
                }
                change_state(SYNC_STATE_ERROR);
            }
            /* GPS time setting handled above state machine */
            break;
            
        case SYNC_STATE_FREQ_CAL:
            /* Wait for frequency counter to stabilize */
            if (!freq_counter_signal_present()) {
                printf("[RB] WARNING: 10MHz signal not detected!\n");
                if (state_time > 30) {
                    change_state(SYNC_STATE_ERROR);
                }
                break;
            }
            
            /* Need at least 10 PPS pulses for calibration */
            if (state_pps_count >= 10) {
                double offset = get_frequency_offset_ppb();
                printf("[RB] Frequency calibration complete: %.3f ppb offset\n", offset);
                
                if (fabs(offset) < 10000) {  /* Within 10 ppm is reasonable */
                    change_state(SYNC_STATE_COARSE);
                } else {
                    printf("[RB] WARNING: Large frequency offset detected\n");
                    change_state(SYNC_STATE_COARSE);  /* Continue anyway */
                }
            }
            break;
            
        case SYNC_STATE_COARSE:
            /* Coarse time acquisition */
            if (!is_pps_valid()) {
                printf("[RB] Lost PPS signal!\n");
                change_state(SYNC_STATE_ERROR);
                break;
            }
            
            /* Wait for time to be set (via NTP or manual) or use default */
            if (epoch_set || state_pps_count >= 10) {
                printf("[RB] Coarse sync complete, entering fine discipline\n");
                change_state(SYNC_STATE_FINE);
            }
            break;
            
        case SYNC_STATE_FINE:
            /* Fine time discipline */
            if (!is_pps_valid()) {
                printf("[RB] Lost PPS signal, entering holdover\n");
                change_state(SYNC_STATE_HOLDOVER);
                break;
            }
            
            if (!rb_locked) {
                printf("[RB] Lost rubidium lock, entering holdover\n");
                change_state(SYNC_STATE_HOLDOVER);
                break;
            }
            
            /* Check if we've achieved lock */
            if (discipline_is_locked() && state_pps_count >= 60) {
                printf("[RB] Time discipline LOCKED - Stratum 1 quality achieved!\n");
                change_state(SYNC_STATE_LOCKED);
                g_time_state.time_valid = true;
            }
            break;
            
        case SYNC_STATE_LOCKED:
            /* Monitor for loss of lock */
            if (!is_pps_valid()) {
                printf("[RB] Lost PPS signal, entering holdover\n");
                change_state(SYNC_STATE_HOLDOVER);
                break;
            }
            
            if (!rb_locked) {
                printf("[RB] Lost rubidium lock, entering holdover\n");
                change_state(SYNC_STATE_HOLDOVER);
                break;
            }
            
            if (!discipline_is_locked()) {
                printf("[RB] Lost time discipline lock, returning to fine sync\n");
                change_state(SYNC_STATE_FINE);
            }
            break;
            
        case SYNC_STATE_HOLDOVER:
            /* Running on stored frequency offset */
            g_time_state.time_valid = (state_time < 3600);  /* Valid for 1 hour */

            if (is_pps_valid() && rb_locked) {
                printf("[RB] PPS and Rb lock restored, returning to fine sync\n");
                change_state(SYNC_STATE_FINE);
            }

            /* Use GPS PPS as backup during holdover if available */
            if (!is_pps_valid() && gps_pps_valid()) {
                static uint32_t last_gps_pps_report = 0;
                if (now / 1000000 - last_gps_pps_report >= 60) {
                    printf("[RB] Using GPS PPS as backup time source\n");
                    last_gps_pps_report = now / 1000000;
                }
                /* Extend holdover validity when GPS PPS is available */
                g_time_state.time_valid = (state_time < 7200);  /* 2 hours with GPS */
            }

            if (state_time > 86400) {  /* 24 hours */
                printf("[RB] Extended holdover, time may be inaccurate\n");
                change_state(SYNC_STATE_ERROR);
            }
            break;
            
        case SYNC_STATE_ERROR:
            /* Wait for conditions to improve */
            g_time_state.time_valid = false;

            if (rb_locked && is_pps_valid()) {
                printf("[RB] Conditions restored, restarting sync\n");
                discipline_reset();
                change_state(SYNC_STATE_FREQ_CAL);
            }

            /* If GPS has valid time and PPS, we can provide degraded service */
            if (gps_has_time() && gps_pps_valid()) {
                static uint32_t last_gps_error_report = 0;
                if (now / 1000000 - last_gps_error_report >= 300) {  /* Every 5 min */
                    printf("[RB] GPS available - degraded stratum 2 service possible\n");
                    last_gps_error_report = now / 1000000;
                }
            }
            break;
    }
}

/*============================================================================
 * TIME API
 *============================================================================*/

/**
 * Initialize time subsystem
 */
void time_init(void) {
    current_seconds = 0;
    subsecond_us = 0;
    epoch_set = false;
    accumulated_offset = 0;
}

/**
 * Get current time as timestamp
 */
timestamp_t get_current_time(void) {
    timestamp_t ts;
    uint64_t now = time_us_64();
    
    /* Calculate subseconds since last PPS */
    uint32_t sub_us;
    uint32_t sec;
    
    uint32_t irq = save_and_disable_interrupts();
    sec = current_seconds;
    if (last_pps_us > 0 && now >= last_pps_us) {
        sub_us = (now - last_pps_us) % 1000000;
    } else {
        sub_us = subsecond_us;
    }
    restore_interrupts(irq);
    
    /* Apply frequency correction */
    double correction = discipline_get_correction();
    int64_t correction_ns = (int64_t)(sub_us * correction / 1e3);
    sub_us -= correction_ns / 1000;
    
    /* Convert to NTP timestamp format */
    ts.seconds = sec + NTP_UNIX_OFFSET + epoch_offset;
    
    /* Convert microseconds to NTP fraction (2^32 / 1e6) */
    ts.fraction = (uint32_t)((uint64_t)sub_us * 4294967296ULL / 1000000ULL);
    
    return ts;
}

/**
 * Get current time in microseconds since some epoch
 */
uint64_t get_time_us(void) {
    uint64_t now = time_us_64();
    
    uint32_t irq = save_and_disable_interrupts();
    uint32_t sec = current_seconds;
    uint64_t pps = last_pps_us;
    restore_interrupts(irq);
    
    if (pps > 0 && now >= pps) {
        return (uint64_t)sec * 1000000ULL + (now - pps);
    }
    
    return (uint64_t)sec * 1000000ULL;
}

/**
 * Set the current time
 */
void set_time(timestamp_t *ts) {
    uint32_t irq = save_and_disable_interrupts();
    
    /* Calculate epoch offset from provided time */
    current_seconds = ts->seconds - NTP_UNIX_OFFSET;
    epoch_offset = 0;
    epoch_set = true;
    
    restore_interrupts(irq);
    
    printf("[RB] Time set to %lu seconds (NTP epoch)\n", ts->seconds);
}

/**
 * Set time from Unix timestamp
 */
void set_time_unix(uint32_t unix_time) {
    uint32_t irq = save_and_disable_interrupts();
    
    current_seconds = unix_time;
    epoch_offset = 0;
    epoch_set = true;
    
    restore_interrupts(irq);
    
    printf("[RB] Time set to Unix timestamp %lu\n", unix_time);
}

/**
 * Check if rubidium oscillator is locked
 */
bool rubidium_is_locked(void) {
    return rb_lock_status;
}

/**
 * Get current sync state
 */
sync_state_t get_sync_state(void) {
    return current_state;
}

/**
 * Get rubidium warmup time in seconds
 */
uint32_t get_rb_warmup_time(void) {
    return rb_warmup_time;
}

/**
 * Get rubidium lock duration in seconds
 */
uint32_t get_rb_lock_duration(void) {
    return rb_lock_duration;
}

/**
 * Force time resync from GPS
 * Clears the epoch flag and GPS state so fresh data will be used
 */
void force_time_resync(void) {
    uint32_t irq = save_and_disable_interrupts();
    epoch_set = false;
    gps_time_pending = false;
    pending_gps_time = 0;
    restore_interrupts(irq);
    gps_reset_time();  /* Flush UART and wait for fresh NMEA */
    printf("[RB] Time resync requested - will sync on next PPS edge\n");
}
