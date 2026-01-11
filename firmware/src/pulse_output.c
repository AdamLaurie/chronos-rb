/**
 * CHRONOS-Rb Configurable Pulse Output with PIO Precision
 *
 * Generates precisely-timed pulses synchronized to PPS using PIO hardware.
 * Sub-microsecond jitter on pulse edges when triggered from PPS.
 *
 * PIO State Machine Allocation:
 *   PIO0 SM1-SM3: 3 pulse outputs (SM0 used by pps_capture)
 *   PIO1 SM2-SM3: 2 pulse outputs (SM0=freq_counter, SM1=pps_generator)
 *   Total: 5 PIO-timed outputs, remaining use CPU fallback
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "chronos_rb.h"
#include "pulse_output.h"
#include "config.h"
#include "pulse_pio.pio.h"
#include "pulse_interval_pio.pio.h"

/*============================================================================
 * PIO ALLOCATION
 *============================================================================*/

/* Maximum PIO-timed outputs */
#define MAX_PIO_OUTPUTS     5

/* PIO SM allocation table */
typedef struct {
    PIO pio;
    uint sm;
    bool allocated;
    int slot;           /* Which pulse_config slot uses this SM */
    bool is_interval;   /* Using interval PIO (10MHz counting) vs PPS PIO */
} pio_sm_alloc_t;

static pio_sm_alloc_t pio_allocs[MAX_PIO_OUTPUTS] = {
    { pio0, 1, false, -1, false },  /* PIO0 SM1 */
    { pio0, 2, false, -1, false },  /* PIO0 SM2 */
    { pio0, 3, false, -1, false },  /* PIO0 SM3 */
    { pio1, 2, false, -1, false },  /* PIO1 SM2 */
    { pio1, 3, false, -1, false },  /* PIO1 SM3 */
};

/* PIO program offsets (loaded once) */
static uint pulse_pio_offset = 0;
static uint pulse_interval_pio_offset = 0;
static bool pulse_pio_loaded = false;
static bool pulse_interval_pio_loaded = false;

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static pulse_config_t pulse_configs[MAX_PULSE_OUTPUTS];
static bool pulse_system_initialized = false;
static uint32_t last_processed_pps = 0;

/*============================================================================
 * CONFIG STORAGE HELPERS
 *============================================================================*/

static void config_to_stored(const pulse_config_t *src, pulse_config_stored_t *dst) {
    dst->gpio_pin = src->gpio_pin;
    dst->mode = (uint8_t)src->mode;
    dst->trigger_second = src->trigger_second;
    dst->trigger_minute = src->trigger_minute;
    dst->trigger_hour = src->trigger_hour;
    dst->active = src->active ? 1 : 0;
    dst->pulse_width_ms = src->pulse_width_ms;
    dst->pulse_count = src->pulse_count;
    dst->pulse_gap_ms = src->pulse_gap_ms;
    dst->interval_ds = src->interval_ds;
}

static void stored_to_config(const pulse_config_stored_t *src, pulse_config_t *dst) {
    memset(dst, 0, sizeof(pulse_config_t));
    dst->gpio_pin = src->gpio_pin;
    dst->mode = (pulse_mode_t)src->mode;
    dst->trigger_second = src->trigger_second;
    dst->trigger_minute = src->trigger_minute;
    dst->trigger_hour = src->trigger_hour;
    dst->active = src->active != 0;
    dst->pulse_width_ms = src->pulse_width_ms;
    dst->pulse_count = src->pulse_count;
    dst->pulse_gap_ms = src->pulse_gap_ms;
    dst->interval_ds = src->interval_ds;
}

static void save_pulse_config(int slot) {
    if (slot < 0 || slot >= MAX_PULSE_OUTPUTS) return;
    pulse_config_stored_t stored;
    config_to_stored(&pulse_configs[slot], &stored);
    config_set_pulse_config(slot, &stored);
}

/*============================================================================
 * PIO MANAGEMENT
 *============================================================================*/

/**
 * Allocate a PIO SM for a pulse output
 * Returns index into pio_allocs, or -1 if none available
 */
static int allocate_pio_sm(int slot, bool need_interval) {
    for (int i = 0; i < MAX_PIO_OUTPUTS; i++) {
        if (!pio_allocs[i].allocated) {
            pio_allocs[i].allocated = true;
            pio_allocs[i].slot = slot;
            pio_allocs[i].is_interval = need_interval;
            return i;
        }
    }
    return -1;  /* No SM available */
}

/**
 * Free a PIO SM
 */
static void free_pio_sm(int slot) {
    for (int i = 0; i < MAX_PIO_OUTPUTS; i++) {
        if (pio_allocs[i].allocated && pio_allocs[i].slot == slot) {
            /* Stop and reset the SM */
            pio_sm_set_enabled(pio_allocs[i].pio, pio_allocs[i].sm, false);
            pio_allocs[i].allocated = false;
            pio_allocs[i].slot = -1;
            break;
        }
    }
}

/**
 * Get PIO allocation for a slot
 */
static pio_sm_alloc_t* get_pio_alloc(int slot) {
    for (int i = 0; i < MAX_PIO_OUTPUTS; i++) {
        if (pio_allocs[i].allocated && pio_allocs[i].slot == slot) {
            return &pio_allocs[i];
        }
    }
    return NULL;
}

/**
 * Initialize PIO for a pulse output
 */
static bool init_pulse_pio(int slot) {
    pulse_config_t *cfg = &pulse_configs[slot];

    /* Determine if we need interval PIO (sub-second) or PPS PIO */
    bool need_interval = (cfg->mode == PULSE_MODE_INTERVAL && cfg->interval_ds < 10);

    /* Allocate SM */
    int alloc_idx = allocate_pio_sm(slot, need_interval);
    if (alloc_idx < 0) {
        printf("[PULSE] No PIO SM available for GPIO %d, using CPU fallback\n", cfg->gpio_pin);
        cfg->pio_index = -1;
        return false;
    }

    pio_sm_alloc_t *alloc = &pio_allocs[alloc_idx];
    cfg->pio_index = alloc_idx;

    /* Load PIO program if not already loaded */
    if (need_interval) {
        if (!pulse_interval_pio_loaded) {
            pulse_interval_pio_offset = pio_add_program(alloc->pio, &pulse_interval_pio_program);
            pulse_interval_pio_loaded = true;
        }
        /* Get active PPS pin */
        uint pps_pin = GPIO_PPS_INPUT;  /* Use Rb PPS as trigger sync */
        pulse_interval_pio_program_init(alloc->pio, alloc->sm, pulse_interval_pio_offset,
                                         GPIO_10MHZ_INPUT, pps_pin, cfg->gpio_pin);
    } else {
        if (!pulse_pio_loaded) {
            pulse_pio_offset = pio_add_program(alloc->pio, &pulse_pio_program);
            pulse_pio_loaded = true;
        }
        pulse_pio_program_init(alloc->pio, alloc->sm, pulse_pio_offset,
                               GPIO_PPS_INPUT, cfg->gpio_pin);
    }

    /* Enable the SM */
    pio_sm_set_enabled(alloc->pio, alloc->sm, true);

    printf("[PULSE] GPIO %d using PIO%d SM%d (%s)\n",
           cfg->gpio_pin, alloc->pio == pio0 ? 0 : 1, alloc->sm,
           need_interval ? "10MHz interval" : "PPS sync");

    return true;
}

/**
 * Load pulse configs from flash and initialize PIO
 */
static void load_pulse_configs(void) {
    pulse_config_stored_t *stored = config_get_pulse_configs();
    int loaded = 0;

    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        if (stored[i].active) {
            stored_to_config(&stored[i], &pulse_configs[i]);

            /* Initialize PIO for this output */
            init_pulse_pio(i);
            loaded++;
        }
    }

    if (loaded > 0) {
        printf("[PULSE] Loaded %d pulse configurations from flash\n", loaded);
    }
}

/*============================================================================
 * HELPER FUNCTIONS
 *============================================================================*/

static int find_slot(uint8_t gpio_pin, bool find_empty) {
    int empty_slot = -1;

    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        if (pulse_configs[i].active && pulse_configs[i].gpio_pin == gpio_pin) {
            return i;
        }
        if (!pulse_configs[i].active && empty_slot < 0) {
            empty_slot = i;
        }
    }

    return find_empty ? empty_slot : -1;
}

static void get_time_components(uint32_t pps_count, uint8_t *second,
                                 uint8_t *minute, uint8_t *hour) {
    timestamp_t ts = get_current_time();
    uint32_t seconds_today = ts.seconds % 86400;

    *hour = (seconds_today / 3600) % 24;
    *minute = (seconds_today / 60) % 60;
    *second = seconds_today % 60;
}

/**
 * Queue a pulse to PIO (fires on next PPS edge)
 */
static void queue_pio_pulse(int slot) {
    pulse_config_t *cfg = &pulse_configs[slot];
    pio_sm_alloc_t *alloc = get_pio_alloc(slot);

    if (!alloc) {
        /* CPU fallback - just set GPIO high briefly */
        gpio_put(cfg->gpio_pin, 1);
        cfg->pulse_off_time = time_us_32() + (cfg->pulse_width_ms * 1000);
        return;
    }

    /* Convert pulse width to microseconds */
    uint32_t width_us = cfg->pulse_width_ms * 1000;

    if (alloc->is_interval) {
        /* Sub-second interval: queue with 10MHz count */
        /* For now, sync to PPS (count=0) - will be enhanced later */
        pulse_interval_pio_queue(alloc->pio, alloc->sm, 0, width_us);
    } else {
        /* PPS-sync: just queue the width, PIO waits for PPS */
        pulse_pio_queue(alloc->pio, alloc->sm, width_us);
    }
}

/**
 * Queue interval pulse with 10MHz offset
 * offset_ds: deciseconds from PPS (0-9)
 */
static void queue_interval_pulse(int slot, uint8_t offset_ds) {
    pulse_config_t *cfg = &pulse_configs[slot];
    pio_sm_alloc_t *alloc = get_pio_alloc(slot);

    if (!alloc) {
        /* CPU fallback */
        gpio_put(cfg->gpio_pin, 1);
        cfg->pulse_off_time = time_us_32() + (cfg->pulse_width_ms * 1000);
        return;
    }

    uint32_t width_us = cfg->pulse_width_ms * 1000;

    if (alloc->is_interval) {
        /* Calculate 10MHz count: offset_ds * 1,000,000 edges */
        uint32_t count = offset_ds * 1000000;
        pulse_interval_pio_queue(alloc->pio, alloc->sm, count, width_us);
    } else {
        /* Using PPS PIO - queue for next edge */
        pulse_pio_queue(alloc->pio, alloc->sm, width_us);
    }
}

/**
 * Start a pulse burst (for multi-pulse bursts, handled by CPU timing between pulses)
 */
static void start_burst(pulse_config_t *cfg, int slot) {
    /* Fire first pulse via PIO */
    queue_pio_pulse(slot);

    /* Set up remaining pulses in burst (CPU-timed gaps) */
    if (cfg->pulse_count > 1) {
        cfg->burst_remaining = cfg->pulse_count - 1;
        cfg->next_pulse_time = time_us_32() + (cfg->pulse_width_ms * 1000) + (cfg->pulse_gap_ms * 1000);
    } else {
        cfg->burst_remaining = 0;
        cfg->next_pulse_time = 0;
    }
}

/**
 * Continue a pulse burst
 */
static void continue_burst(pulse_config_t *cfg, int slot) {
    /* Fire next pulse via PIO (immediate, not PPS-synced) */
    pio_sm_alloc_t *alloc = get_pio_alloc(slot);

    if (alloc) {
        uint32_t width_us = cfg->pulse_width_ms * 1000;
        if (alloc->is_interval) {
            /* Fire immediately (count=1 for minimal delay) */
            pulse_interval_pio_queue(alloc->pio, alloc->sm, 1, width_us);
        } else {
            /* For PPS PIO, we need to use CPU for burst continuation */
            gpio_put(cfg->gpio_pin, 1);
            cfg->pulse_off_time = time_us_32() + (cfg->pulse_width_ms * 1000);
        }
    } else {
        gpio_put(cfg->gpio_pin, 1);
        cfg->pulse_off_time = time_us_32() + (cfg->pulse_width_ms * 1000);
    }

    cfg->burst_remaining--;

    if (cfg->burst_remaining > 0) {
        cfg->next_pulse_time = time_us_32() + (cfg->pulse_width_ms * 1000) + (cfg->pulse_gap_ms * 1000);
    } else {
        cfg->next_pulse_time = 0;
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

void pulse_output_init(void) {
    memset(pulse_configs, 0, sizeof(pulse_configs));

    /* Reset PIO allocations */
    for (int i = 0; i < MAX_PIO_OUTPUTS; i++) {
        pio_allocs[i].allocated = false;
        pio_allocs[i].slot = -1;
    }

    pulse_system_initialized = true;

    /* Load saved configurations from flash */
    load_pulse_configs();

    printf("[PULSE] PIO pulse output system initialized\n");
}

void pulse_output_task(void) {
    if (!pulse_system_initialized) {
        return;
    }

    uint32_t now = time_us_32();
    uint32_t pps_count = g_time_state.pps_count;

    /* Detect new PPS edge */
    bool new_pps = (pps_count != last_processed_pps);
    if (new_pps) {
        last_processed_pps = pps_count;
    }

    /* Get current time components */
    uint8_t cur_second, cur_minute, cur_hour;
    get_time_components(pps_count, &cur_second, &cur_minute, &cur_hour);

    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *cfg = &pulse_configs[i];

        if (!cfg->active) {
            continue;
        }

        /* Handle CPU fallback pulse off timing */
        if (cfg->pulse_off_time != 0 && now >= cfg->pulse_off_time) {
            gpio_put(cfg->gpio_pin, 0);
            cfg->pulse_off_time = 0;
        }

        /* Handle next pulse in burst */
        if (cfg->next_pulse_time != 0 && now >= cfg->next_pulse_time &&
            cfg->pulse_off_time == 0) {
            continue_burst(cfg, i);
        }

        /* Skip if currently in a burst */
        if (cfg->burst_remaining > 0 || cfg->pulse_off_time != 0) {
            continue;
        }

        switch (cfg->mode) {
            case PULSE_MODE_INTERVAL: {
                /* Fire every N deciseconds (0.1s) */
                if (pps_count == 0) break;

                /* For intervals >= 1s, trigger on PPS edge */
                if (cfg->interval_ds >= 10) {
                    /* Check if this PPS should trigger */
                    uint32_t interval_sec = cfg->interval_ds / 10;
                    if (new_pps && (pps_count % interval_sec) == 0) {
                        /* Avoid double-trigger */
                        if (pps_count != cfg->last_trigger_pps) {
                            start_burst(cfg, i);
                            cfg->last_trigger_pps = pps_count;
                        }
                    }
                } else {
                    /* Sub-second interval - use 10MHz counting */
                    uint64_t last_pps = get_active_pps_timestamp();
                    if (last_pps == 0) break;

                    uint32_t us_since_pps = (uint32_t)((now > last_pps) ? (now - last_pps) : 0);
                    if (us_since_pps > 1000000) us_since_pps = 0;
                    uint8_t cur_ds = us_since_pps / 100000;

                    uint32_t total_ds = pps_count * 10 + cur_ds;

                    if ((total_ds % cfg->interval_ds) == 0) {
                        if (pps_count != cfg->last_trigger_pps || cur_ds != cfg->last_trigger_ds) {
                            /* Queue with 10MHz offset for precise timing */
                            queue_interval_pulse(i, cur_ds);
                            cfg->last_trigger_pps = pps_count;
                            cfg->last_trigger_ds = cur_ds;
                        }
                    }
                }
                break;
            }

            case PULSE_MODE_SECOND:
                if (cur_second == cfg->trigger_second) {
                    if (!cfg->triggered_this_period && new_pps) {
                        start_burst(cfg, i);
                        cfg->triggered_this_period = true;
                    }
                } else {
                    cfg->triggered_this_period = false;
                }
                break;

            case PULSE_MODE_MINUTE:
                if (cur_minute == cfg->trigger_minute && cur_second == 0) {
                    if (!cfg->triggered_this_period && new_pps) {
                        start_burst(cfg, i);
                        cfg->triggered_this_period = true;
                    }
                } else if (cur_second != 0) {
                    cfg->triggered_this_period = false;
                }
                break;

            case PULSE_MODE_TIME:
                if (cur_hour == cfg->trigger_hour &&
                    cur_minute == cfg->trigger_minute &&
                    cur_second == 0) {
                    if (!cfg->triggered_this_period && new_pps) {
                        start_burst(cfg, i);
                        cfg->triggered_this_period = true;
                    }
                } else if (cur_second != 0) {
                    cfg->triggered_this_period = false;
                }
                break;

            default:
                break;
        }
    }
}

int pulse_output_set_interval(uint8_t gpio_pin, uint16_t interval_ds,
                              uint16_t pulse_width_ms) {
    if (interval_ds == 0) {
        printf("Error: Interval must be > 0\n");
        return -1;
    }

    int slot = find_slot(gpio_pin, true);
    if (slot < 0) {
        printf("Error: No free pulse slots\n");
        return -1;
    }

    /* Free any existing PIO allocation */
    free_pio_sm(slot);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_INTERVAL;
    cfg->interval_ds = interval_ds;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = 1;
    cfg->pulse_gap_ms = 0;
    cfg->active = true;

    /* Initialize PIO */
    init_pulse_pio(slot);

    printf("[PULSE] GPIO %d: interval %u.%u sec, width %u ms (PIO)\n",
           gpio_pin, interval_ds / 10, interval_ds % 10, pulse_width_ms);

    save_pulse_config(slot);
    return slot;
}

int pulse_output_set_second(uint8_t gpio_pin, uint8_t second,
                            uint16_t pulse_width_ms, uint16_t count,
                            uint16_t gap_ms) {
    if (second > 59) {
        printf("Error: Second must be 0-59\n");
        return -1;
    }
    if (count == 0) {
        printf("Error: Count must be >= 1\n");
        return -1;
    }

    int slot = find_slot(gpio_pin, true);
    if (slot < 0) {
        printf("Error: No free pulse slots\n");
        return -1;
    }

    free_pio_sm(slot);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_SECOND;
    cfg->trigger_second = second;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = count;
    cfg->pulse_gap_ms = gap_ms;
    cfg->active = true;

    init_pulse_pio(slot);

    printf("[PULSE] GPIO %d: on second %u, %u ms pulse x%u (gap %u ms) (PIO)\n",
           gpio_pin, second, pulse_width_ms, count, gap_ms);

    save_pulse_config(slot);
    return slot;
}

int pulse_output_set_minute(uint8_t gpio_pin, uint8_t minute,
                            uint16_t pulse_width_ms, uint16_t count,
                            uint16_t gap_ms) {
    if (minute > 59) {
        printf("Error: Minute must be 0-59\n");
        return -1;
    }
    if (count == 0) {
        printf("Error: Count must be >= 1\n");
        return -1;
    }

    int slot = find_slot(gpio_pin, true);
    if (slot < 0) {
        printf("Error: No free pulse slots\n");
        return -1;
    }

    free_pio_sm(slot);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_MINUTE;
    cfg->trigger_minute = minute;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = count;
    cfg->pulse_gap_ms = gap_ms;
    cfg->active = true;

    init_pulse_pio(slot);

    printf("[PULSE] GPIO %d: on minute %u, %u ms pulse x%u (gap %u ms) (PIO)\n",
           gpio_pin, minute, pulse_width_ms, count, gap_ms);

    save_pulse_config(slot);
    return slot;
}

int pulse_output_set_time(uint8_t gpio_pin, uint8_t hour, uint8_t minute,
                          uint16_t pulse_width_ms, uint16_t count,
                          uint16_t gap_ms) {
    if (hour > 23) {
        printf("Error: Hour must be 0-23\n");
        return -1;
    }
    if (minute > 59) {
        printf("Error: Minute must be 0-59\n");
        return -1;
    }
    if (count == 0) {
        printf("Error: Count must be >= 1\n");
        return -1;
    }

    int slot = find_slot(gpio_pin, true);
    if (slot < 0) {
        printf("Error: No free pulse slots\n");
        return -1;
    }

    free_pio_sm(slot);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_TIME;
    cfg->trigger_hour = hour;
    cfg->trigger_minute = minute;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = count;
    cfg->pulse_gap_ms = gap_ms;
    cfg->active = true;

    init_pulse_pio(slot);

    printf("[PULSE] GPIO %d: at %02u:%02u, %u ms pulse x%u (gap %u ms) (PIO)\n",
           gpio_pin, hour, minute, pulse_width_ms, count, gap_ms);

    save_pulse_config(slot);
    return slot;
}

bool pulse_output_disable(uint8_t gpio_pin) {
    int slot = find_slot(gpio_pin, false);
    if (slot < 0) {
        printf("Error: No pulse configured on GPIO %d\n", gpio_pin);
        return false;
    }

    /* Free PIO SM */
    free_pio_sm(slot);

    gpio_put(gpio_pin, 0);
    pulse_configs[slot].active = false;

    save_pulse_config(slot);

    printf("[PULSE] GPIO %d disabled\n", gpio_pin);
    return true;
}

void pulse_output_list(void) {
    printf("\nConfigured pulse outputs:\n");
    printf("─────────────────────────────────────────────────────────\n");

    bool any_active = false;
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *cfg = &pulse_configs[i];
        if (!cfg->active) {
            continue;
        }
        any_active = true;

        printf("  [%d] GPIO %2d: ", i, cfg->gpio_pin);

        switch (cfg->mode) {
            case PULSE_MODE_INTERVAL:
                printf("every %u.%u sec", cfg->interval_ds / 10, cfg->interval_ds % 10);
                break;
            case PULSE_MODE_SECOND:
                printf("on second %u", cfg->trigger_second);
                break;
            case PULSE_MODE_MINUTE:
                printf("on minute %u", cfg->trigger_minute);
                break;
            case PULSE_MODE_TIME:
                printf("at %02u:%02u", cfg->trigger_hour, cfg->trigger_minute);
                break;
            default:
                printf("???");
                break;
        }

        printf(", %u ms", cfg->pulse_width_ms);

        if (cfg->pulse_count > 1) {
            printf(" x%u (gap %u ms)", cfg->pulse_count, cfg->pulse_gap_ms);
        }

        /* Show PIO status */
        pio_sm_alloc_t *alloc = get_pio_alloc(i);
        if (alloc) {
            printf(" [PIO%d SM%d]", alloc->pio == pio0 ? 0 : 1, alloc->sm);
        } else {
            printf(" [CPU]");
        }

        if (cfg->burst_remaining > 0) {
            printf(" [burst: %u remaining]", cfg->burst_remaining);
        }

        printf("\n");
    }

    if (!any_active) {
        printf("  (none)\n");
    }
    printf("\n");
}

pulse_config_t* pulse_output_get(int index) {
    if (index < 0 || index >= MAX_PULSE_OUTPUTS) {
        return NULL;
    }
    return &pulse_configs[index];
}

void pulse_output_clear_all(void) {
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        if (pulse_configs[i].active) {
            free_pio_sm(i);
            gpio_put(pulse_configs[i].gpio_pin, 0);
        }
    }
    memset(pulse_configs, 0, sizeof(pulse_configs));

    config_clear_pulse_configs();

    printf("[PULSE] All pulse outputs cleared\n");
}
