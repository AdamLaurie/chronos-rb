/**
 * CHRONOS-Rb Configurable Pulse Output
 *
 * Configurable GPIO pulse outputs with interval and time-based triggers
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "chronos_rb.h"
#include "pulse_output.h"

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static pulse_config_t pulse_configs[MAX_PULSE_OUTPUTS];
static bool pulse_system_initialized = false;

/*============================================================================
 * HELPER FUNCTIONS
 *============================================================================*/

/**
 * Find an existing config for a GPIO pin, or an empty slot
 */
static int find_slot(uint8_t gpio_pin, bool find_empty) {
    int empty_slot = -1;

    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        if (pulse_configs[i].active && pulse_configs[i].gpio_pin == gpio_pin) {
            return i;  /* Found existing config for this pin */
        }
        if (!pulse_configs[i].active && empty_slot < 0) {
            empty_slot = i;
        }
    }

    return find_empty ? empty_slot : -1;
}

/**
 * Get current time components from PPS count
 * Note: This is a simplified version - actual time would come from NTP/RTC
 */
static void get_time_components(uint32_t pps_count, uint8_t *second,
                                 uint8_t *minute, uint8_t *hour) {
    /* Get actual time from the time state if available */
    timestamp_t ts = get_current_time();

    /* Convert NTP timestamp to time of day */
    /* NTP epoch is 1900, but we just need time of day */
    uint32_t seconds_today = ts.seconds % 86400;

    *hour = (seconds_today / 3600) % 24;
    *minute = (seconds_today / 60) % 60;
    *second = seconds_today % 60;
}

/**
 * Start a pulse burst - fires first pulse and sets up remaining
 */
static void start_burst(pulse_config_t *cfg) {
    uint32_t now = time_us_32();

    /* Fire first pulse */
    gpio_put(cfg->gpio_pin, 1);
    cfg->pulse_off_time = now + (cfg->pulse_width_ms * 1000);

    /* Set up remaining pulses in burst */
    if (cfg->pulse_count > 1) {
        cfg->burst_remaining = cfg->pulse_count - 1;
        cfg->next_pulse_time = cfg->pulse_off_time + (cfg->pulse_gap_ms * 1000);
    } else {
        cfg->burst_remaining = 0;
        cfg->next_pulse_time = 0;
    }
}

/**
 * Continue a pulse burst - fires next pulse
 */
static void continue_burst(pulse_config_t *cfg) {
    uint32_t now = time_us_32();

    /* Fire next pulse */
    gpio_put(cfg->gpio_pin, 1);
    cfg->pulse_off_time = now + (cfg->pulse_width_ms * 1000);
    cfg->burst_remaining--;

    /* Schedule next pulse if more remain */
    if (cfg->burst_remaining > 0) {
        cfg->next_pulse_time = cfg->pulse_off_time + (cfg->pulse_gap_ms * 1000);
    } else {
        cfg->next_pulse_time = 0;
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize pulse output system
 */
void pulse_output_init(void) {
    memset(pulse_configs, 0, sizeof(pulse_configs));
    pulse_system_initialized = true;
    printf("[PULSE] Pulse output system initialized\n");
}

/**
 * Process pulse outputs - call from main loop
 */
void pulse_output_task(void) {
    if (!pulse_system_initialized) {
        return;
    }

    uint32_t now = time_us_32();
    uint32_t pps_count = g_time_state.pps_count;

    /* Get current time components */
    uint8_t cur_second, cur_minute, cur_hour;
    get_time_components(pps_count, &cur_second, &cur_minute, &cur_hour);

    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *cfg = &pulse_configs[i];

        if (!cfg->active) {
            continue;
        }

        /* Handle pulse off timing */
        if (cfg->pulse_off_time != 0 && now >= cfg->pulse_off_time) {
            gpio_put(cfg->gpio_pin, 0);
            cfg->pulse_off_time = 0;
        }

        /* Handle next pulse in burst */
        if (cfg->next_pulse_time != 0 && now >= cfg->next_pulse_time &&
            cfg->pulse_off_time == 0) {
            continue_burst(cfg);
        }

        /* Check if we should start a new burst based on mode */
        /* Only start if not currently in a burst */
        if (cfg->burst_remaining > 0 || cfg->pulse_off_time != 0) {
            continue;
        }

        switch (cfg->mode) {
            case PULSE_MODE_INTERVAL:
                /* Fire every N seconds based on PPS count */
                if (pps_count > 0 && pps_count != cfg->last_trigger_pps) {
                    if ((pps_count % cfg->interval) == 0) {
                        start_burst(cfg);
                        cfg->last_trigger_pps = pps_count;
                    }
                }
                break;

            case PULSE_MODE_SECOND:
                /* Fire on specific second each minute */
                if (cur_second == cfg->trigger_second) {
                    if (!cfg->triggered_this_period) {
                        start_burst(cfg);
                        cfg->triggered_this_period = true;
                    }
                } else {
                    cfg->triggered_this_period = false;
                }
                break;

            case PULSE_MODE_MINUTE:
                /* Fire on specific minute each hour (at second 0) */
                if (cur_minute == cfg->trigger_minute && cur_second == 0) {
                    if (!cfg->triggered_this_period) {
                        start_burst(cfg);
                        cfg->triggered_this_period = true;
                    }
                } else if (cur_second != 0) {
                    cfg->triggered_this_period = false;
                }
                break;

            case PULSE_MODE_TIME:
                /* Fire at specific HH:MM (at second 0) */
                if (cur_hour == cfg->trigger_hour &&
                    cur_minute == cfg->trigger_minute &&
                    cur_second == 0) {
                    if (!cfg->triggered_this_period) {
                        start_burst(cfg);
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

/**
 * Configure interval-based pulse
 */
int pulse_output_set_interval(uint8_t gpio_pin, uint32_t interval_sec,
                              uint16_t pulse_width_ms) {
    if (interval_sec == 0) {
        printf("Error: Interval must be > 0\n");
        return -1;
    }

    int slot = find_slot(gpio_pin, true);
    if (slot < 0) {
        printf("Error: No free pulse slots\n");
        return -1;
    }

    /* Initialize GPIO */
    gpio_init(gpio_pin);
    gpio_set_dir(gpio_pin, GPIO_OUT);
    gpio_put(gpio_pin, 0);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_INTERVAL;
    cfg->interval = interval_sec;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = 1;    /* Single pulse for interval mode */
    cfg->pulse_gap_ms = 0;
    cfg->active = true;

    printf("[PULSE] GPIO %d: interval %lu sec, width %u ms\n",
           gpio_pin, interval_sec, pulse_width_ms);

    return slot;
}

/**
 * Configure second-triggered pulse
 */
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

    /* Initialize GPIO */
    gpio_init(gpio_pin);
    gpio_set_dir(gpio_pin, GPIO_OUT);
    gpio_put(gpio_pin, 0);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_SECOND;
    cfg->trigger_second = second;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = count;
    cfg->pulse_gap_ms = gap_ms;
    cfg->active = true;

    printf("[PULSE] GPIO %d: on second %u, %u ms pulse x%u (gap %u ms)\n",
           gpio_pin, second, pulse_width_ms, count, gap_ms);

    return slot;
}

/**
 * Configure minute-triggered pulse
 */
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

    /* Initialize GPIO */
    gpio_init(gpio_pin);
    gpio_set_dir(gpio_pin, GPIO_OUT);
    gpio_put(gpio_pin, 0);

    pulse_config_t *cfg = &pulse_configs[slot];
    memset(cfg, 0, sizeof(pulse_config_t));

    cfg->gpio_pin = gpio_pin;
    cfg->mode = PULSE_MODE_MINUTE;
    cfg->trigger_minute = minute;
    cfg->pulse_width_ms = pulse_width_ms;
    cfg->pulse_count = count;
    cfg->pulse_gap_ms = gap_ms;
    cfg->active = true;

    printf("[PULSE] GPIO %d: on minute %u, %u ms pulse x%u (gap %u ms)\n",
           gpio_pin, minute, pulse_width_ms, count, gap_ms);

    return slot;
}

/**
 * Configure time-triggered pulse
 */
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

    /* Initialize GPIO */
    gpio_init(gpio_pin);
    gpio_set_dir(gpio_pin, GPIO_OUT);
    gpio_put(gpio_pin, 0);

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

    printf("[PULSE] GPIO %d: at %02u:%02u, %u ms pulse x%u (gap %u ms)\n",
           gpio_pin, hour, minute, pulse_width_ms, count, gap_ms);

    return slot;
}

/**
 * Disable a pulse output
 */
bool pulse_output_disable(uint8_t gpio_pin) {
    int slot = find_slot(gpio_pin, false);
    if (slot < 0) {
        printf("Error: No pulse configured on GPIO %d\n", gpio_pin);
        return false;
    }

    gpio_put(gpio_pin, 0);
    pulse_configs[slot].active = false;

    printf("[PULSE] GPIO %d disabled\n", gpio_pin);
    return true;
}

/**
 * List all pulse configurations
 */
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
                printf("every %lu sec", cfg->interval);
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

/**
 * Get pulse configuration by index
 */
pulse_config_t* pulse_output_get(int index) {
    if (index < 0 || index >= MAX_PULSE_OUTPUTS) {
        return NULL;
    }
    return &pulse_configs[index];
}

/**
 * Clear all pulse configurations
 */
void pulse_output_clear_all(void) {
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        if (pulse_configs[i].active) {
            gpio_put(pulse_configs[i].gpio_pin, 0);
        }
    }
    memset(pulse_configs, 0, sizeof(pulse_configs));
    printf("[PULSE] All pulse outputs cleared\n");
}
