/**
 * CHRONOS-Rb Legacy Time Protocols
 *
 * TIME Protocol (RFC 868) and Daytime Protocol (RFC 867)
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef TIME_PROTOCOL_H
#define TIME_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize TIME (RFC 868) and Daytime (RFC 867) servers
 */
void time_protocols_init(void);

/**
 * Get request statistics
 * @param time_reqs     Output: TIME protocol requests served
 * @param daytime_reqs  Output: Daytime protocol requests served
 */
void time_protocols_get_stats(uint32_t *time_reqs, uint32_t *daytime_reqs);

#endif /* TIME_PROTOCOL_H */
