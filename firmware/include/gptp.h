/**
 * CHRONOS-Rb gPTP (IEEE 802.1AS) Support
 *
 * Time-Sensitive Networking profile of PTP
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef GPTP_H
#define GPTP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize gPTP support
 */
void gptp_init(void);

/**
 * gPTP task - call from main loop
 */
void gptp_task(void);

/**
 * Enable/disable gPTP
 */
void gptp_enable(bool enable);

/**
 * Check if gPTP is enabled
 */
bool gptp_is_enabled(void);

/**
 * Get measured peer delay in nanoseconds
 */
int64_t gptp_get_peer_delay(void);

/**
 * Get statistics
 */
void gptp_get_stats(uint32_t *sync_sent, uint32_t *pdelay_req,
                    uint32_t *pdelay_resp);

#endif /* GPTP_H */
