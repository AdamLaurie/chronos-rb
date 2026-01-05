/**
 * CHRONOS-Rb NTS (Network Time Security) Support
 *
 * RFC 8915 authenticated NTP
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef NTS_H
#define NTS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize NTS support
 * Note: Full NTS requires TLS 1.3 (mbedTLS)
 */
void nts_init(void);

/**
 * Check if NTP request has NTS extensions
 */
bool nts_is_protected_request(const uint8_t *pkt, size_t len);

/**
 * Process NTS-protected NTP request
 * Returns response length, 0 if cannot process
 */
int nts_process_request(const uint8_t *req, size_t req_len,
                        uint8_t *resp, size_t max_resp_len);

/**
 * Enable/disable NTS
 */
void nts_enable(bool enable);

/**
 * Check if NTS is enabled
 */
bool nts_is_enabled(void);

/**
 * Get statistics
 */
void nts_get_stats(uint32_t *ke_conns, uint32_t *ntp_reqs);

/**
 * Check if full NTS is available (requires TLS)
 */
bool nts_is_fully_implemented(void);

#endif /* NTS_H */
