/**
 * CHRONOS-Rb Log Buffer
 *
 * Ring buffer for capturing log output for web interface display.
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* LOG_BUFFER_SIZE is defined in chronos_rb.h */

/**
 * Initialize log buffer and hook into stdio
 */
void log_buffer_init(void);

/**
 * Get log contents since last read position
 * Returns number of bytes written to buf (not including null terminator)
 * Updates read position for next call
 */
size_t log_buffer_read(char *buf, size_t buf_size, uint32_t *read_pos);

/**
 * Get current write position (for tracking new data)
 */
uint32_t log_buffer_get_pos(void);

/**
 * Clear the log buffer
 */
void log_buffer_clear(void);

#endif /* LOG_BUFFER_H */
