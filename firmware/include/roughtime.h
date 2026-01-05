/**
 * CHRONOS-Rb Roughtime Protocol Server
 *
 * Google's Roughtime protocol for rough time with cryptographic proof
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef ROUGHTIME_H
#define ROUGHTIME_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize Roughtime server on UDP port 2002
 */
void roughtime_init(void);

/**
 * Enable/disable Roughtime server
 */
void roughtime_enable(bool enable);

/**
 * Check if Roughtime is enabled
 */
bool roughtime_is_enabled(void);

/**
 * Get request count
 */
uint32_t roughtime_get_requests(void);

/**
 * Get public key (32 bytes)
 */
const uint8_t *roughtime_get_pubkey(void);

#endif /* ROUGHTIME_H */
