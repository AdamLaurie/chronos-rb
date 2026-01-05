/**
 * CHRONOS-Rb NMEA 0183 Output
 *
 * GPS-compatible time sentences over UART
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef NMEA_OUTPUT_H
#define NMEA_OUTPUT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize NMEA output on UART1 (GP28)
 */
void nmea_output_init(void);

/**
 * NMEA task - call from main loop to output sentences
 */
void nmea_output_task(void);

/**
 * Enable/disable NMEA output
 */
void nmea_output_enable(bool enable);

/**
 * Check if NMEA output is enabled
 */
bool nmea_output_is_enabled(void);

/**
 * Get number of sentences sent
 */
uint32_t nmea_output_get_count(void);

#endif /* NMEA_OUTPUT_H */
