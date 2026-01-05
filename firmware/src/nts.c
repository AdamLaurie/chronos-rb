/**
 * CHRONOS-Rb NTS (Network Time Security) Support
 *
 * NTS (RFC 8915) provides authenticated and encrypted NTP.
 * It consists of two protocols:
 *
 * 1. NTS-KE (Key Establishment) - TCP port 4460
 *    - TLS 1.3 handshake to establish keys
 *    - Exports keying material for NTP authentication
 *    - Provides cookies for stateless server operation
 *
 * 2. NTS-protected NTP - UDP port 123
 *    - Standard NTP with NTS extension fields
 *    - AEAD encryption (AES-SIV recommended)
 *    - Cookie-based key management
 *
 * IMPLEMENTATION STATUS:
 * This is a skeleton implementation. Full NTS requires:
 * - TLS 1.3 library (mbedTLS or similar)
 * - AEAD implementation (AES-SIV or AES-GCM)
 * - ~50KB additional flash for crypto
 *
 * For now, this module provides:
 * - Protocol structure definitions
 * - Extension field parsing
 * - Placeholder for future TLS integration
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

#include "chronos_rb.h"
#include "nts.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define NTS_KE_PORT         4460    /* NTS Key Establishment */
#define NTS_NTP_PORT        123     /* Same as NTP */

/* NTS-KE record types */
#define NTS_KE_END          0       /* End of message */
#define NTS_KE_NEXT_PROTO   1       /* Next protocol negotiation */
#define NTS_KE_ERROR        2       /* Error */
#define NTS_KE_WARNING      3       /* Warning */
#define NTS_KE_AEAD_ALGO    4       /* AEAD algorithm negotiation */
#define NTS_KE_COOKIE       5       /* New cookie */
#define NTS_KE_SERVER       6       /* NTPv4 server negotiation */
#define NTS_KE_PORT_NEG     7       /* NTPv4 port negotiation */

/* AEAD algorithm IDs */
#define AEAD_AES_SIV_CMAC_256   15  /* Recommended */
#define AEAD_AES_128_GCM_SIV    30

/* NTP extension field types for NTS */
#define EF_NTS_UNIQUE_ID    0x0104  /* Unique identifier */
#define EF_NTS_COOKIE       0x0204  /* NTS cookie */
#define EF_NTS_COOKIE_PLAC  0x0304  /* Cookie placeholder */
#define EF_NTS_AUTH         0x0404  /* NTS authenticator */

/* Cookie size */
#define NTS_COOKIE_SIZE     128

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct tcp_pcb *nts_ke_pcb = NULL;
static bool nts_enabled = false;
static uint32_t nts_ke_connections = 0;
static uint32_t nts_ntp_requests = 0;

/* Server master key (for cookie encryption - MUST be randomly generated) */
static uint8_t master_key[32] = {
    /* Placeholder - should be generated at first boot and stored in flash */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

/*============================================================================
 * NTS-KE RECORD BUILDING
 *============================================================================*/

/**
 * Build NTS-KE response (placeholder - needs TLS wrapper)
 *
 * A real implementation would:
 * 1. Complete TLS 1.3 handshake
 * 2. Export keying material using TLS exporter
 * 3. Generate cookies encrypted with server key
 * 4. Send NTS-KE records over TLS
 */
static int build_nts_ke_response(uint8_t *buf, size_t max_len) {
    int pos = 0;

    /* Record: Next Protocol (NTPv4 = 0) */
    buf[pos++] = 0x80 | NTS_KE_NEXT_PROTO;  /* Critical bit set */
    buf[pos++] = NTS_KE_NEXT_PROTO;
    buf[pos++] = 0x00;  /* Length MSB */
    buf[pos++] = 0x02;  /* Length LSB */
    buf[pos++] = 0x00;  /* Protocol ID MSB */
    buf[pos++] = 0x00;  /* Protocol ID LSB (NTPv4) */

    /* Record: AEAD Algorithm (AES-SIV-CMAC-256) */
    buf[pos++] = 0x80 | NTS_KE_AEAD_ALGO;
    buf[pos++] = NTS_KE_AEAD_ALGO;
    buf[pos++] = 0x00;
    buf[pos++] = 0x02;
    buf[pos++] = 0x00;
    buf[pos++] = AEAD_AES_SIV_CMAC_256;

    /* Record: Cookie (placeholder - needs real encryption) */
    /* In real implementation, cookie contains:
     * - AEAD algorithm
     * - S2C key (encrypted)
     * - C2S key (encrypted)
     * All encrypted with server master key */
    buf[pos++] = NTS_KE_COOKIE;
    buf[pos++] = NTS_KE_COOKIE;
    buf[pos++] = 0x00;
    buf[pos++] = NTS_COOKIE_SIZE;
    /* Placeholder cookie data */
    memset(buf + pos, 0xCC, NTS_COOKIE_SIZE);
    pos += NTS_COOKIE_SIZE;

    /* Provide 8 cookies (recommended) */
    for (int i = 0; i < 7; i++) {
        buf[pos++] = NTS_KE_COOKIE;
        buf[pos++] = NTS_KE_COOKIE;
        buf[pos++] = 0x00;
        buf[pos++] = NTS_COOKIE_SIZE;
        memset(buf + pos, 0xCC + i, NTS_COOKIE_SIZE);
        pos += NTS_COOKIE_SIZE;
    }

    /* Record: End of message */
    buf[pos++] = 0x80 | NTS_KE_END;
    buf[pos++] = NTS_KE_END;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    return pos;
}

/*============================================================================
 * NTP EXTENSION FIELD PARSING
 *============================================================================*/

/**
 * Parse NTS extension fields from NTP packet
 * Returns true if valid NTS request
 */
static bool parse_nts_extensions(const uint8_t *pkt, size_t len,
                                 uint8_t *cookie, size_t *cookie_len,
                                 uint8_t *unique_id, size_t *unique_id_len) {
    /* NTP packet is 48 bytes minimum, extensions follow */
    if (len <= 48) return false;

    size_t pos = 48;
    bool has_cookie = false;
    bool has_unique_id = false;
    bool has_auth = false;

    while (pos + 4 <= len) {
        uint16_t field_type = (pkt[pos] << 8) | pkt[pos + 1];
        uint16_t field_len = (pkt[pos + 2] << 8) | pkt[pos + 3];

        /* Field length must be multiple of 4 and include header */
        if (field_len < 4 || (field_len & 3) != 0) break;
        if (pos + field_len > len) break;

        switch (field_type) {
            case EF_NTS_UNIQUE_ID:
                if (unique_id && unique_id_len && field_len > 4) {
                    *unique_id_len = field_len - 4;
                    memcpy(unique_id, pkt + pos + 4, *unique_id_len);
                    has_unique_id = true;
                }
                break;

            case EF_NTS_COOKIE:
                if (cookie && cookie_len && field_len > 4) {
                    *cookie_len = field_len - 4;
                    memcpy(cookie, pkt + pos + 4, *cookie_len);
                    has_cookie = true;
                }
                break;

            case EF_NTS_AUTH:
                has_auth = true;
                /* Would verify AEAD tag here */
                break;
        }

        pos += field_len;
    }

    return has_cookie && has_unique_id && has_auth;
}

/*============================================================================
 * TCP CALLBACKS (NTS-KE)
 *============================================================================*/

/**
 * NTS-KE connection accepted
 *
 * Note: This is a placeholder. Real implementation needs:
 * 1. TLS 1.3 handshake (requires mbedTLS integration)
 * 2. ALPN negotiation for "ntske/1"
 * 3. Key export after handshake
 */
static err_t nts_ke_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    nts_ke_connections++;

    /*
     * TODO: Integrate TLS 1.3
     *
     * For now, just close the connection with an error message.
     * Real implementation would:
     * 1. Start TLS handshake
     * 2. After handshake, send NTS-KE records
     * 3. Close connection
     */

    printf("[NTS] KE connection from client (TLS not implemented)\n");

    /* Send a placeholder response and close */
    const char *msg = "NTS-KE requires TLS 1.3\r\n";
    tcp_write(newpcb, msg, strlen(msg), TCP_WRITE_FLAG_COPY);
    tcp_output(newpcb);
    tcp_close(newpcb);

    return ERR_OK;
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize NTS support
 */
void nts_init(void) {
    printf("[NTS] Initializing Network Time Security\n");
    printf("[NTS] WARNING: TLS 1.3 not implemented - NTS-KE will reject connections\n");
    printf("[NTS] Full NTS requires mbedTLS integration\n");

    /* Create NTS-KE TCP listener */
    nts_ke_pcb = tcp_new();
    if (nts_ke_pcb == NULL) {
        printf("[NTS] Failed to create TCP PCB\n");
        return;
    }

    err_t err = tcp_bind(nts_ke_pcb, IP_ADDR_ANY, NTS_KE_PORT);
    if (err != ERR_OK) {
        printf("[NTS] Failed to bind port %d: %d\n", NTS_KE_PORT, err);
        tcp_close(nts_ke_pcb);
        nts_ke_pcb = NULL;
        return;
    }

    nts_ke_pcb = tcp_listen(nts_ke_pcb);
    if (nts_ke_pcb == NULL) {
        printf("[NTS] Failed to listen\n");
        return;
    }

    tcp_accept(nts_ke_pcb, nts_ke_accept);

    nts_enabled = true;
    printf("[NTS] NTS-KE listening on TCP port %d (placeholder)\n", NTS_KE_PORT);
}

/**
 * Check if NTP request has NTS extensions
 * Returns true if this is an NTS-protected request
 */
bool nts_is_protected_request(const uint8_t *pkt, size_t len) {
    if (len <= 48) return false;

    /* Check for NTS extension fields */
    size_t pos = 48;
    while (pos + 4 <= len) {
        uint16_t field_type = (pkt[pos] << 8) | pkt[pos + 1];
        uint16_t field_len = (pkt[pos + 2] << 8) | pkt[pos + 3];

        if (field_type == EF_NTS_COOKIE || field_type == EF_NTS_AUTH) {
            return true;
        }

        if (field_len < 4) break;
        pos += field_len;
    }

    return false;
}

/**
 * Process NTS-protected NTP request
 * Returns response length, 0 if not valid NTS request
 *
 * Note: This is a placeholder. Real implementation needs AEAD.
 */
int nts_process_request(const uint8_t *req, size_t req_len,
                        uint8_t *resp, size_t max_resp_len) {
    (void)req;
    (void)req_len;
    (void)resp;
    (void)max_resp_len;

    nts_ntp_requests++;

    /* Would decrypt, verify, process, and encrypt response */
    printf("[NTS] NTS-protected NTP request (AEAD not implemented)\n");

    return 0;  /* Cannot process without AEAD */
}

/**
 * Enable/disable NTS
 */
void nts_enable(bool enable) {
    nts_enabled = enable;
    printf("[NTS] %s\n", enable ? "Enabled" : "Disabled");
}

/**
 * Check if NTS is enabled
 */
bool nts_is_enabled(void) {
    return nts_enabled;
}

/**
 * Get statistics
 */
void nts_get_stats(uint32_t *ke_conns, uint32_t *ntp_reqs) {
    if (ke_conns) *ke_conns = nts_ke_connections;
    if (ntp_reqs) *ntp_reqs = nts_ntp_requests;
}

/**
 * Check if full NTS is available (requires TLS)
 */
bool nts_is_fully_implemented(void) {
    return false;  /* TLS not implemented */
}
