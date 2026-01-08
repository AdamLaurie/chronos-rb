/**
 * CHRONOS-Rb Log Buffer
 *
 * Ring buffer for capturing log output for web interface display.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "chronos_rb.h"
#include "log_buffer.h"

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static char log_ring[LOG_BUFFER_SIZE];
static volatile uint32_t write_pos = 0;
static volatile uint32_t total_written = 0;  /* Total bytes ever written (for tracking) */
static bool initialized = false;

/*============================================================================
 * STDIO DRIVER
 *============================================================================*/

/**
 * Custom putchar that writes to ring buffer
 */
static void log_out_chars(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        log_ring[write_pos] = buf[i];
        write_pos = (write_pos + 1) % LOG_BUFFER_SIZE;
        total_written++;
    }
}

/* Custom stdio driver that captures output */
static stdio_driver_t log_stdio_driver = {
    .out_chars = log_out_chars,
    .out_flush = NULL,
    .in_chars = NULL,
    .set_chars_available_callback = NULL,
    .next = NULL,
};

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize log buffer and hook into stdio
 */
void log_buffer_init(void) {
    if (initialized) {
        return;
    }

    memset(log_ring, 0, sizeof(log_ring));
    write_pos = 0;
    total_written = 0;

    /* Add our driver to capture all printf output */
    stdio_set_driver_enabled(&log_stdio_driver, true);

    initialized = true;
}

/**
 * Get log contents since given position
 * read_pos is the total_written value from last read (or 0 for all)
 * Returns bytes written to buf, updates read_pos to current position
 */
size_t log_buffer_read(char *buf, size_t buf_size, uint32_t *read_pos) {
    if (!initialized || buf_size == 0) {
        return 0;
    }

    uint32_t current_total = total_written;
    uint32_t last_read = *read_pos;

    /* Calculate how much new data is available */
    uint32_t available = current_total - last_read;

    /* If buffer has wrapped and we've lost data, start from oldest available */
    if (available > LOG_BUFFER_SIZE) {
        available = LOG_BUFFER_SIZE;
        last_read = current_total - LOG_BUFFER_SIZE;
    }

    /* Limit to output buffer size (leave room for null) */
    if (available > buf_size - 1) {
        available = buf_size - 1;
        last_read = current_total - available;
    }

    /* Copy data from ring buffer */
    size_t copied = 0;
    uint32_t ring_pos = last_read % LOG_BUFFER_SIZE;

    for (uint32_t i = 0; i < available; i++) {
        buf[copied++] = log_ring[ring_pos];
        ring_pos = (ring_pos + 1) % LOG_BUFFER_SIZE;
    }

    buf[copied] = '\0';
    *read_pos = current_total;

    return copied;
}

/**
 * Get current write position for tracking
 */
uint32_t log_buffer_get_pos(void) {
    return total_written;
}

/**
 * Clear the log buffer
 */
void log_buffer_clear(void) {
    write_pos = 0;
    total_written = 0;
    memset(log_ring, 0, sizeof(log_ring));
}
