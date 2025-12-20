/**
 * CHRONOS-Rb: Compact High-precision Rubidium Oscillator Network Operating System
 * 
 * Main entry point for Raspberry Pi Pico 2-W NTP/PTP Server
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"

#include "chronos_rb.h"

/*============================================================================
 * GLOBAL VARIABLES
 *============================================================================*/

volatile time_state_t g_time_state = {0};
volatile statistics_t g_stats = {0};
volatile bool g_wifi_connected = false;

/* Configuration (loaded from flash or defaults) */
static char wifi_ssid[33] = WIFI_SSID_DEFAULT;
static char wifi_pass[65] = WIFI_PASS_DEFAULT;

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize all GPIO pins
 */
void gpio_init_all(void) {
    /* Initialize 1PPS input with pull-down */
    gpio_init(GPIO_PPS_INPUT);
    gpio_set_dir(GPIO_PPS_INPUT, GPIO_IN);
    gpio_pull_down(GPIO_PPS_INPUT);
    
    /* Initialize 10MHz input */
    gpio_init(GPIO_10MHZ_INPUT);
    gpio_set_dir(GPIO_10MHZ_INPUT, GPIO_IN);
    
    /* Initialize Rubidium lock status input (active low) */
    gpio_init(GPIO_RB_LOCK_STATUS);
    gpio_set_dir(GPIO_RB_LOCK_STATUS, GPIO_IN);
    gpio_pull_up(GPIO_RB_LOCK_STATUS);
    
    /* Initialize optional enable output */
    gpio_init(GPIO_RB_ENABLE);
    gpio_set_dir(GPIO_RB_ENABLE, GPIO_OUT);
    gpio_put(GPIO_RB_ENABLE, 1);  /* Enable by default */
    
    /* Initialize debug outputs */
    gpio_init(GPIO_DEBUG_PPS_OUT);
    gpio_set_dir(GPIO_DEBUG_PPS_OUT, GPIO_OUT);
    gpio_put(GPIO_DEBUG_PPS_OUT, 0);
    
    gpio_init(GPIO_DEBUG_SYNC_PULSE);
    gpio_set_dir(GPIO_DEBUG_SYNC_PULSE, GPIO_OUT);
    gpio_put(GPIO_DEBUG_SYNC_PULSE, 0);
    
    /* Initialize interval pulse outputs */
    gpio_init(GPIO_PULSE_500MS);
    gpio_set_dir(GPIO_PULSE_500MS, GPIO_OUT);
    gpio_put(GPIO_PULSE_500MS, 0);
    
    gpio_init(GPIO_PULSE_1S);
    gpio_set_dir(GPIO_PULSE_1S, GPIO_OUT);
    gpio_put(GPIO_PULSE_1S, 0);
    
    gpio_init(GPIO_PULSE_6S);
    gpio_set_dir(GPIO_PULSE_6S, GPIO_OUT);
    gpio_put(GPIO_PULSE_6S, 0);
    
    gpio_init(GPIO_PULSE_30S);
    gpio_set_dir(GPIO_PULSE_30S, GPIO_OUT);
    gpio_put(GPIO_PULSE_30S, 0);
    
    gpio_init(GPIO_PULSE_60S);
    gpio_set_dir(GPIO_PULSE_60S, GPIO_OUT);
    gpio_put(GPIO_PULSE_60S, 0);
}

/**
 * Initialize status LEDs
 */
void led_init(void) {
    gpio_init(GPIO_LED_SYNC);
    gpio_set_dir(GPIO_LED_SYNC, GPIO_OUT);
    gpio_put(GPIO_LED_SYNC, 0);
    
    gpio_init(GPIO_LED_NETWORK);
    gpio_set_dir(GPIO_LED_NETWORK, GPIO_OUT);
    gpio_put(GPIO_LED_NETWORK, 0);
    
    gpio_init(GPIO_LED_ACTIVITY);
    gpio_set_dir(GPIO_LED_ACTIVITY, GPIO_OUT);
    gpio_put(GPIO_LED_ACTIVITY, 0);
    
    gpio_init(GPIO_LED_ERROR);
    gpio_set_dir(GPIO_LED_ERROR, GPIO_OUT);
    gpio_put(GPIO_LED_ERROR, 0);
}

/**
 * LED control functions
 */
void led_set_sync(bool on) {
    gpio_put(GPIO_LED_SYNC, on);
}

void led_set_network(bool on) {
    gpio_put(GPIO_LED_NETWORK, on);
}

void led_set_activity(bool on) {
    gpio_put(GPIO_LED_ACTIVITY, on);
}

void led_set_error(bool on) {
    gpio_put(GPIO_LED_ERROR, on);
}

static uint32_t activity_off_time = 0;

/*============================================================================
 * INTERVAL PULSE GENERATION
 *============================================================================*/

/* Pulse state tracking */
static struct {
    uint32_t pulse_off_time_500ms;
    uint32_t pulse_off_time_1s;
    uint32_t pulse_off_time_6s;
    uint32_t pulse_off_time_30s;
    uint32_t pulse_off_time_60s;
    uint32_t last_pps_count;
    uint32_t half_second_toggle;
} pulse_state = {0};

/**
 * Generate interval pulses synchronized to PPS
 * Called from main loop, generates pulses at 0.5s, 1s, 6s, 30s, and 60s intervals
 */
static void update_interval_pulses(void) {
    uint32_t now = time_us_32();
    uint32_t pps_count = g_time_state.pps_count;
    
    /* Check for new PPS edge (1-second boundary) */
    if (pps_count != pulse_state.last_pps_count) {
        pulse_state.last_pps_count = pps_count;
        pulse_state.half_second_toggle = 0;
        
        /* 1-second pulse - every PPS */
        gpio_put(GPIO_PULSE_1S, 1);
        pulse_state.pulse_off_time_1s = now + (PULSE_WIDTH_MS * 1000);
        
        /* 6-second pulse */
        if ((pps_count % 6) == 0) {
            gpio_put(GPIO_PULSE_6S, 1);
            pulse_state.pulse_off_time_6s = now + (PULSE_WIDTH_MS * 1000);
        }
        
        /* 30-second pulse */
        if ((pps_count % 30) == 0) {
            gpio_put(GPIO_PULSE_30S, 1);
            pulse_state.pulse_off_time_30s = now + (PULSE_WIDTH_MS * 1000);
        }
        
        /* 60-second pulse (1 minute) */
        if ((pps_count % 60) == 0) {
            gpio_put(GPIO_PULSE_60S, 1);
            pulse_state.pulse_off_time_60s = now + (PULSE_WIDTH_MS * 1000);
        }
        
        /* 0.5-second pulse on PPS edge */
        gpio_put(GPIO_PULSE_500MS, 1);
        pulse_state.pulse_off_time_500ms = now + (PULSE_WIDTH_MS * 1000);
    }
    
    /* Generate 0.5-second pulse at mid-point between PPS edges */
    if (!pulse_state.half_second_toggle) {
        /* Check if 500ms has elapsed since last PPS */
        uint64_t last_pps_time = get_last_pps_timestamp();
        if (last_pps_time > 0 && (now - (uint32_t)last_pps_time) >= 500000) {
            pulse_state.half_second_toggle = 1;
            gpio_put(GPIO_PULSE_500MS, 1);
            pulse_state.pulse_off_time_500ms = now + (PULSE_WIDTH_MS * 1000);
        }
    }
    
    /* Turn off pulses after pulse width expires */
    if (pulse_state.pulse_off_time_500ms && now >= pulse_state.pulse_off_time_500ms) {
        gpio_put(GPIO_PULSE_500MS, 0);
        pulse_state.pulse_off_time_500ms = 0;
    }
    if (pulse_state.pulse_off_time_1s && now >= pulse_state.pulse_off_time_1s) {
        gpio_put(GPIO_PULSE_1S, 0);
        pulse_state.pulse_off_time_1s = 0;
    }
    if (pulse_state.pulse_off_time_6s && now >= pulse_state.pulse_off_time_6s) {
        gpio_put(GPIO_PULSE_6S, 0);
        pulse_state.pulse_off_time_6s = 0;
    }
    if (pulse_state.pulse_off_time_30s && now >= pulse_state.pulse_off_time_30s) {
        gpio_put(GPIO_PULSE_30S, 0);
        pulse_state.pulse_off_time_30s = 0;
    }
    if (pulse_state.pulse_off_time_60s && now >= pulse_state.pulse_off_time_60s) {
        gpio_put(GPIO_PULSE_60S, 0);
        pulse_state.pulse_off_time_60s = 0;
    }
}

void led_blink_activity(void) {
    gpio_put(GPIO_LED_ACTIVITY, 1);
    activity_off_time = time_us_32() + 50000;  /* 50ms pulse */
}

/**
 * Startup LED sequence for visual feedback
 */
static void led_startup_sequence(void) {
    const uint leds[] = {GPIO_LED_SYNC, GPIO_LED_NETWORK, GPIO_LED_ACTIVITY, GPIO_LED_ERROR};
    
    /* Sequential blink */
    for (int i = 0; i < 4; i++) {
        gpio_put(leds[i], 1);
        sleep_ms(100);
        gpio_put(leds[i], 0);
    }
    
    /* All on briefly */
    for (int i = 0; i < 4; i++) {
        gpio_put(leds[i], 1);
    }
    sleep_ms(200);
    for (int i = 0; i < 4; i++) {
        gpio_put(leds[i], 0);
    }
}

/**
 * Print startup banner
 */
static void print_banner(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                      CHRONOS-Rb v%s                       ║\n", CHRONOS_VERSION_STRING);
    printf("║  Compact High-precision Rubidium Oscillator Network System   ║\n");
    printf("║                                                              ║\n");
    printf("║  Raspberry Pi Pico 2-W NTP/PTP Server                        ║\n");
    printf("║  Synchronized to FE-5680A Rubidium Frequency Standard        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Build: %s %s\n", CHRONOS_BUILD_DATE, CHRONOS_BUILD_TIME);
    printf("System Clock: %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
    printf("\n");
}

/**
 * Main system initialization
 */
void chronos_init(void) {
    /* Initialize stdio for debug output */
    stdio_init_all();
    
    /* Small delay for USB enumeration */
    sleep_ms(2000);
    
    print_banner();
    
    printf("[INIT] Initializing GPIO...\n");
    gpio_init_all();
    
    printf("[INIT] Initializing LEDs...\n");
    led_init();
    led_startup_sequence();
    
    printf("[INIT] Initializing time subsystem...\n");
    time_init();
    
    printf("[INIT] Initializing PPS capture...\n");
    pps_capture_init();
    
    printf("[INIT] Initializing frequency counter...\n");
    freq_counter_init();
    
    printf("[INIT] Initializing time discipline...\n");
    discipline_init();
    
    printf("[INIT] Initializing rubidium sync...\n");
    rubidium_sync_init();
    
    printf("[INIT] Initializing WiFi...\n");
    wifi_init();
    
    printf("[INIT] Initialization complete!\n\n");
}

/*============================================================================
 * MAIN LOOP
 *============================================================================*/

/**
 * Update LED states based on system status
 */
static void update_status_leds(void) {
    /* Sync LED: solid when locked, blinking when acquiring */
    static uint32_t sync_blink_time = 0;
    static bool sync_blink_state = false;
    
    if (g_time_state.sync_state == SYNC_STATE_LOCKED) {
        led_set_sync(true);
    } else if (g_time_state.sync_state >= SYNC_STATE_FREQ_CAL) {
        if (time_us_32() > sync_blink_time) {
            sync_blink_state = !sync_blink_state;
            led_set_sync(sync_blink_state);
            sync_blink_time = time_us_32() + 500000;  /* 500ms blink */
        }
    } else {
        led_set_sync(false);
    }
    
    /* Network LED */
    led_set_network(g_wifi_connected);
    
    /* Activity LED auto-off */
    if (activity_off_time && time_us_32() > activity_off_time) {
        gpio_put(GPIO_LED_ACTIVITY, 0);
        activity_off_time = 0;
    }
    
    /* Error LED */
    led_set_error(g_time_state.sync_state == SYNC_STATE_ERROR);
}

/**
 * Print periodic status
 */
static void print_status(void) {
    static uint32_t last_status_time = 0;
    
    if (time_us_32() - last_status_time > 10000000) {  /* Every 10 seconds */
        last_status_time = time_us_32();
        
        const char *sync_states[] = {
            "INIT", "FREQ_CAL", "COARSE", "FINE", "LOCKED", "HOLDOVER", "ERROR"
        };
        
        printf("\n[STATUS] Sync: %s | Rb Lock: %s | PPS: %lu | Freq: %lu Hz\n",
               sync_states[g_time_state.sync_state],
               g_time_state.rb_locked ? "YES" : "NO",
               g_time_state.pps_count,
               g_time_state.last_freq_count);
        
        printf("[STATUS] Offset: %lld ns | Freq Offset: %.3f ppb\n",
               g_time_state.offset_ns,
               g_time_state.frequency_offset);
        
        printf("[STATUS] NTP Requests: %lu | PTP Sync: %lu | Errors: %lu\n",
               g_stats.ntp_requests,
               g_stats.ptp_sync_sent,
               g_stats.errors);
        
        if (g_wifi_connected) {
            extern uint32_t get_ip_address(void);
            uint32_t ip = get_ip_address();
            printf("[STATUS] IP: %lu.%lu.%lu.%lu\n",
                   ip & 0xFF, (ip >> 8) & 0xFF,
                   (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        }
    }
}

/**
 * Main entry point
 */
int main(void) {
    /* Set system clock to 150MHz for better timing resolution */
    set_sys_clock_khz(150000, true);
    
    /* Initialize everything */
    chronos_init();
    
    /* Try to connect to WiFi */
    printf("[WIFI] Connecting to %s...\n", wifi_ssid);
    if (wifi_connect(wifi_ssid, wifi_pass)) {
        printf("[WIFI] Connected!\n");
        g_wifi_connected = true;
        
        /* Start network services */
        printf("[NTP] Starting NTP server on port %d...\n", NTP_PORT);
        ntp_server_init();
        
        printf("[PTP] Starting PTP server on ports %d/%d...\n", 
               PTP_EVENT_PORT, PTP_GENERAL_PORT);
        ptp_server_init();
        
        printf("[WEB] Starting web interface on port %d...\n", WEB_PORT);
        web_init();
    } else {
        printf("[WIFI] Connection failed, starting AP mode...\n");
        /* TODO: Implement AP mode for configuration */
    }
    
    printf("\n[MAIN] Entering main loop...\n\n");
    
    /* Enable watchdog with 8 second timeout */
    watchdog_enable(8000, 1);
    
    /* Main loop */
    while (1) {
        /* Feed watchdog */
        watchdog_update();
        
        /* Run all tasks */
        rubidium_sync_task();
        
        if (g_wifi_connected) {
            wifi_task();
            ntp_server_task();
            ptp_server_task();
            web_task();
        }
        
        /* Update status LEDs */
        update_status_leds();
        
        /* Generate interval pulses */
        update_interval_pulses();
        
        /* Print periodic status */
        print_status();
        
        /* Small delay to prevent tight loop */
        sleep_us(100);
    }
    
    return 0;
}
