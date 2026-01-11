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
#include "gnss_input.h"

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

/* GNSS time synchronization state */
static uint32_t pending_gnss_time = 0;    /* GNSS time to set on next PPS */
static bool gnss_time_pending = false;    /* True if we have a GNSS time waiting */

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
 *
 * Architecture: GNSS is primary time reference, Rb provides frequency stability
 * - GNSS PPS: Primary time source (absolute accuracy)
 * - Rb 10MHz: Frequency reference (stability)
 * - Rb PPS: Backup/holdover when GNSS lost
 */
void rubidium_sync_init(void) {
    printf("[RB] Initializing time synchronization\n");
    printf("[RB] Primary: GNSS PPS | Frequency: Rb 10MHz | Backup: Rb PPS\n");

    current_state = SYNC_STATE_INIT;
    state_enter_time = time_us_64();
    state_pps_count = 0;

    /* Initialize time to a default (will be set via NTP or manual) */
    current_seconds = 0;
    subsecond_us = 0;

    g_time_state.sync_state = current_state;
    g_time_state.time_valid = false;
    g_time_state.rb_locked = false;

    printf("[RB] Waiting for GNSS lock (primary) or Rb lock (backup)...\n");
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

    /* Apply pending GNSS time if waiting
     * GNSS NMEA arrives ~300ms after the PPS it refers to
     * So pending_gnss_time is the time of the PREVIOUS GNSS second
     * This PPS marks the start of the NEXT second */
    if (gnss_time_pending) {
        current_seconds = pending_gnss_time + 1;
        epoch_offset = 0;
        gnss_time_pending = false;
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

    /* Queue GNSS time if not already set (will be applied on next PPS edge)
     * GNSS NMEA arrives ~300ms after the PPS pulse it describes
     * So we queue the time and apply it on the next PPS edge with +1 second */
    if (!epoch_set && !gnss_time_pending && gnss_has_time()) {
        uint32_t gnss_time = gnss_get_unix_time();
        if (gnss_time > 0) {
            printf("[RB] Queueing GNSS time %lu for next PPS edge\n", gnss_time);
            pending_gnss_time = gnss_time;
            gnss_time_pending = true;
        }
    }

    /* State machine */
    uint64_t state_time = (now - state_enter_time) / 1000000;  /* Seconds in state */
    
    switch (current_state) {
        case SYNC_STATE_INIT:
            /* Wait for GNSS lock (primary) or Rb lock (backup) */
            if (gnss_has_time() && gnss_pps_valid()) {
                printf("[RB] GNSS locked - primary time source acquired\n");
                change_state(SYNC_STATE_FREQ_CAL);
            } else if (rb_locked) {
                printf("[RB] Rb locked after %lu seconds (GNSS not available)\n", rb_warmup_time);
                printf("[RB] Using Rb as time source until GNSS acquired\n");
                change_state(SYNC_STATE_FREQ_CAL);
            } else if (state_time > 600) {  /* 10 minute timeout */
                printf("[RB] ERROR: Neither GNSS nor Rb locked within 10 minutes\n");
                change_state(SYNC_STATE_ERROR);
            }
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
            /* Coarse time acquisition - need GNSS or Rb PPS */
            if (!gnss_pps_valid() && !is_pps_valid()) {
                printf("[RB] Lost all PPS signals!\n");
                change_state(SYNC_STATE_ERROR);
                break;
            }

            /* Wait for time to be set (via GNSS or NTP) or use default */
            if (epoch_set || state_pps_count >= 10) {
                printf("[RB] Coarse sync complete, entering fine discipline\n");
                if (gnss_pps_valid()) {
                    printf("[RB] Using GNSS PPS as primary reference\n");
                } else {
                    printf("[RB] Using Rb PPS (GNSS not available)\n");
                }
                change_state(SYNC_STATE_FINE);
            }
            break;
            
        case SYNC_STATE_FINE:
            /* Fine time discipline - GNSS primary, Rb backup */
            if (!gnss_pps_valid() && !is_pps_valid()) {
                printf("[RB] Lost all PPS signals, entering holdover\n");
                change_state(SYNC_STATE_HOLDOVER);
                break;
            }

            /* Warn if GNSS lost but Rb still available */
            if (!gnss_pps_valid() && is_pps_valid()) {
                static uint64_t last_gnss_warn = 0;
                if (now - last_gnss_warn > 60000000) {  /* Every 60s */
                    printf("[RB] GNSS PPS lost, using Rb PPS as backup\n");
                    last_gnss_warn = now;
                }
            }

            /* Check if we've achieved lock */
            if (discipline_is_locked() && state_pps_count >= 60) {
                printf("[RB] Time discipline LOCKED - Stratum 1 quality achieved!\n");
                change_state(SYNC_STATE_LOCKED);
                g_time_state.time_valid = true;
            }
            break;
            
        case SYNC_STATE_LOCKED:
            /* Monitor for loss of lock - GNSS primary, Rb backup */
            if (!gnss_pps_valid() && !is_pps_valid()) {
                printf("[RB] Lost all PPS signals, entering holdover\n");
                change_state(SYNC_STATE_HOLDOVER);
                break;
            }

            /* Warn if GNSS lost but continue with Rb */
            if (!gnss_pps_valid() && is_pps_valid()) {
                static uint64_t last_gnss_lock_warn = 0;
                if (now - last_gnss_lock_warn > 300000000) {  /* Every 5 min */
                    printf("[RB] GNSS PPS lost, maintaining lock with Rb backup\n");
                    last_gnss_lock_warn = now;
                }
            }

            if (!discipline_is_locked()) {
                printf("[RB] Lost time discipline lock, returning to fine sync\n");
                change_state(SYNC_STATE_FINE);
            }
            break;
            
        case SYNC_STATE_HOLDOVER:
            /* Running on stored frequency offset - Rb provides holdover stability */
            g_time_state.time_valid = (state_time < 3600);  /* Valid for 1 hour */

            /* Check if GNSS (primary) restored */
            if (gnss_pps_valid() && gnss_has_time()) {
                printf("[RB] GNSS restored, returning to fine sync\n");
                change_state(SYNC_STATE_FINE);
                break;
            }

            /* Use Rb PPS as backup during GNSS holdover if available */
            if (!gnss_pps_valid() && is_pps_valid() && rb_locked) {
                static uint32_t last_rb_backup_report = 0;
                if (now / 1000000 - last_rb_backup_report >= 60) {
                    printf("[RB] Using Rb PPS as backup (GNSS unavailable)\n");
                    last_rb_backup_report = now / 1000000;
                }
                /* Extended holdover validity with Rb backup */
                g_time_state.time_valid = (state_time < 7200);  /* 2 hours with Rb */

                /* If Rb is stable, can return to fine sync */
                if (rb_lock_duration > 300) {  /* Rb stable for 5+ min */
                    printf("[RB] Rb stable, returning to fine sync (GNSS-degraded mode)\n");
                    change_state(SYNC_STATE_FINE);
                }
            }

            if (state_time > 86400) {  /* 24 hours */
                printf("[RB] Extended holdover, time may be inaccurate\n");
                change_state(SYNC_STATE_ERROR);
            }
            break;
            
        case SYNC_STATE_ERROR:
            /* Wait for conditions to improve - prioritize GNSS recovery */
            g_time_state.time_valid = false;

            /* GNSS restored - return to normal operation */
            if (gnss_has_time() && gnss_pps_valid()) {
                printf("[RB] GNSS restored, restarting sync\n");
                discipline_reset();
                change_state(SYNC_STATE_FREQ_CAL);
                break;
            }

            /* Rb available as backup */
            if (rb_locked && is_pps_valid()) {
                printf("[RB] Rb available as backup, restarting sync (degraded mode)\n");
                discipline_reset();
                change_state(SYNC_STATE_FREQ_CAL);
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
 * Force time resync from GNSS
 * Clears the epoch flag and GNSS state so fresh data will be used
 */
void force_time_resync(void) {
    uint32_t irq = save_and_disable_interrupts();
    epoch_set = false;
    gnss_time_pending = false;
    pending_gnss_time = 0;
    restore_interrupts(irq);
    gnss_reset_time();  /* Flush UART and wait for fresh NMEA */
    printf("[RB] Time resync requested - will sync on next PPS edge\n");
}
