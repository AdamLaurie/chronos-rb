/**
 * CHRONOS-Rb Roughtime Protocol Server
 *
 * Roughtime is Google's protocol for rough time synchronization with
 * cryptographic proof. It provides a signed timestamp that clients can
 * verify, with ~10 second accuracy (suitable for certificate validation).
 *
 * Protocol: UDP port 2002
 * Signature: Ed25519 (requires crypto library)
 *
 * Note: This implementation provides the protocol framework. Full
 * cryptographic signing requires an Ed25519 implementation.
 *
 * Reference: https://roughtime.googlesource.com/roughtime
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"

#include "chronos_rb.h"
#include "roughtime.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define ROUGHTIME_PORT      2002

/* Tag values (4-byte ASCII) */
#define TAG_SIG     0x00474953  /* "SIG\0" - Signature */
#define TAG_PATH    0x48544150  /* "PATH" - Merkle tree path */
#define TAG_SREP    0x50455253  /* "SREP" - Signed response */
#define TAG_CERT    0x54524543  /* "CERT" - Certificate */
#define TAG_INDX    0x58444E49  /* "INDX" - Index */
#define TAG_PUBK    0x4B425550  /* "PUBK" - Public key */
#define TAG_MIDP    0x5044494D  /* "MIDP" - Midpoint */
#define TAG_RADI    0x49444152  /* "RADI" - Radius */
#define TAG_ROOT    0x544F4F52  /* "ROOT" - Merkle root */
#define TAG_NONC    0x434E4F4E  /* "NONC" - Nonce */

/* Response structure sizes */
#define SIGNATURE_SIZE      64      /* Ed25519 signature */
#define PUBKEY_SIZE         32      /* Ed25519 public key */
#define NONCE_SIZE          64      /* Client nonce */
#define TIMESTAMP_SIZE      8       /* Microseconds since epoch */
#define RADIUS_SIZE         4       /* Uncertainty in microseconds */

/* NTP to Unix offset */
#define NTP_UNIX_OFFSET     2208988800ULL
/* Unix to Roughtime offset (Roughtime uses microseconds since Unix epoch) */

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct udp_pcb *roughtime_pcb = NULL;
static uint32_t roughtime_requests = 0;
static bool roughtime_enabled = true;

/* Dummy Ed25519 keypair (MUST be replaced with real keys in production) */
static const uint8_t dummy_pubkey[PUBKEY_SIZE] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
};

/*============================================================================
 * ROUGHTIME MESSAGE BUILDING
 *============================================================================*/

/**
 * Write a 32-bit little-endian value
 */
static void write_le32(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xff;
    buf[1] = (val >> 8) & 0xff;
    buf[2] = (val >> 16) & 0xff;
    buf[3] = (val >> 24) & 0xff;
}

/**
 * Write a 64-bit little-endian value
 */
static void write_le64(uint8_t *buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf[i] = (val >> (i * 8)) & 0xff;
    }
}

/**
 * Build Roughtime response
 * Returns response size, 0 on error
 *
 * Note: This creates an UNSIGNED response. Real implementation
 * requires Ed25519 signing of the SREP content.
 */
static int build_response(uint8_t *resp, size_t max_len,
                          const uint8_t *nonce, size_t nonce_len) {
    if (max_len < 256) return 0;

    /* Get current time as microseconds since Unix epoch */
    timestamp_t ts = get_current_time();
    uint64_t unix_secs = ts.seconds - NTP_UNIX_OFFSET;
    uint64_t midp_us = unix_secs * 1000000ULL +
                       ((uint64_t)ts.fraction * 1000000ULL >> 32);

    /* Radius (uncertainty) in microseconds - we claim 1ms precision */
    uint32_t radi_us = 1000;

    /*
     * Roughtime message format:
     * - 4 bytes: number of tags
     * - 4 bytes per tag offset (n-1 offsets for n tags)
     * - 4 bytes per tag value
     * - variable: tag data
     */

    int pos = 0;

    /* Build SREP (signed response) content first */
    uint8_t srep[128];  /* Increased buffer size */
    int srep_pos = 0;

    /* SREP contains: ROOT, MIDP, RADI */
    write_le32(srep + srep_pos, 3);  /* 3 tags */
    srep_pos += 4;

    /* Offsets (n-1 = 2 offsets) */
    write_le32(srep + srep_pos, 32);  /* ROOT ends at offset 32 from data start */
    srep_pos += 4;
    write_le32(srep + srep_pos, 40);  /* MIDP ends at offset 40 */
    srep_pos += 4;

    /* Tags */
    write_le32(srep + srep_pos, TAG_ROOT);
    srep_pos += 4;
    write_le32(srep + srep_pos, TAG_MIDP);
    srep_pos += 4;
    write_le32(srep + srep_pos, TAG_RADI);
    srep_pos += 4;

    /* ROOT (Merkle root - hash of nonce, simplified to nonce hash) */
    /* In real implementation, this is SHA-512 hash */
    memset(srep + srep_pos, 0, 32);  /* Placeholder */
    if (nonce_len >= 32) {
        memcpy(srep + srep_pos, nonce, 32);  /* Use nonce as placeholder */
    }
    srep_pos += 32;

    /* MIDP (timestamp midpoint) */
    write_le64(srep + srep_pos, midp_us);
    srep_pos += 8;

    /* RADI (radius/uncertainty) */
    write_le32(srep + srep_pos, radi_us);
    srep_pos += 4;

    /* Now build outer response: SIG, PATH, SREP, CERT, INDX */
    write_le32(resp + pos, 5);  /* 5 tags */
    pos += 4;

    /* Offsets for 5 tags (4 offsets) */
    int sig_end = 64;                      /* SIG: 64 bytes */
    int path_end = sig_end + 0;            /* PATH: empty for single response */
    int srep_end = path_end + srep_pos;    /* SREP: srep_pos bytes */
    int cert_end = srep_end + 0;           /* CERT: empty (use long-term cert) */

    write_le32(resp + pos, sig_end);
    pos += 4;
    write_le32(resp + pos, path_end);
    pos += 4;
    write_le32(resp + pos, srep_end);
    pos += 4;
    write_le32(resp + pos, cert_end);
    pos += 4;

    /* Tags */
    write_le32(resp + pos, TAG_SIG);
    pos += 4;
    write_le32(resp + pos, TAG_PATH);
    pos += 4;
    write_le32(resp + pos, TAG_SREP);
    pos += 4;
    write_le32(resp + pos, TAG_CERT);
    pos += 4;
    write_le32(resp + pos, TAG_INDX);
    pos += 4;

    /* SIG (signature - DUMMY, needs Ed25519 implementation) */
    memset(resp + pos, 0xAA, SIGNATURE_SIZE);  /* Placeholder signature */
    pos += SIGNATURE_SIZE;

    /* PATH (empty for single request) */

    /* SREP */
    memcpy(resp + pos, srep, srep_pos);
    pos += srep_pos;

    /* CERT (empty - would contain delegated certificate) */

    /* INDX (index into batch - 0 for single response) */
    write_le32(resp + pos, 0);
    pos += 4;

    return pos;
}

/*============================================================================
 * UDP CALLBACK
 *============================================================================*/

/**
 * UDP receive callback for Roughtime
 */
static void roughtime_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *addr, u16_t port) {
    (void)arg;

    if (p == NULL || !roughtime_enabled) {
        if (p) pbuf_free(p);
        return;
    }

    /* Parse request - minimum is a NONC tag */
    if (p->tot_len < 12) {  /* 4 (num_tags) + 4 (tag) + 4 (min data) */
        pbuf_free(p);
        return;
    }

    /* Extract nonce from request (simplified parsing) */
    uint8_t nonce[NONCE_SIZE];
    size_t nonce_len = 0;

    uint8_t *data = (uint8_t *)p->payload;
    uint32_t num_tags = data[0] | (data[1] << 8) |
                        (data[2] << 16) | (data[3] << 24);

    /* Look for NONC tag - simplified, assumes it's the only tag */
    if (num_tags >= 1 && p->tot_len >= 12) {
        /* Skip header, get nonce data */
        int data_start = 4 + (num_tags - 1) * 4 + num_tags * 4;
        if (data_start < (int)p->tot_len) {
            nonce_len = p->tot_len - data_start;
            if (nonce_len > NONCE_SIZE) nonce_len = NONCE_SIZE;
            memcpy(nonce, data + data_start, nonce_len);
        }
    }

    pbuf_free(p);

    /* Build response */
    uint8_t resp_buf[512];
    int resp_len = build_response(resp_buf, sizeof(resp_buf), nonce, nonce_len);

    if (resp_len > 0) {
        struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, resp_len, PBUF_RAM);
        if (resp) {
            memcpy(resp->payload, resp_buf, resp_len);
            udp_sendto(pcb, resp, addr, port);
            pbuf_free(resp);
            roughtime_requests++;
        }
    }
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize Roughtime server
 */
void roughtime_init(void) {
    printf("[ROUGHTIME] Initializing on UDP port %d\n", ROUGHTIME_PORT);
    printf("[ROUGHTIME] WARNING: Using dummy keys - not cryptographically secure!\n");
    printf("[ROUGHTIME] For production, implement Ed25519 signing\n");

    roughtime_pcb = udp_new();
    if (roughtime_pcb == NULL) {
        printf("[ROUGHTIME] Failed to create UDP PCB\n");
        return;
    }

    err_t err = udp_bind(roughtime_pcb, IP_ADDR_ANY, ROUGHTIME_PORT);
    if (err != ERR_OK) {
        printf("[ROUGHTIME] Failed to bind: %d\n", err);
        udp_remove(roughtime_pcb);
        roughtime_pcb = NULL;
        return;
    }

    udp_recv(roughtime_pcb, roughtime_recv, NULL);
    printf("[ROUGHTIME] Server listening\n");
}

/**
 * Enable/disable Roughtime server
 */
void roughtime_enable(bool enable) {
    roughtime_enabled = enable;
    printf("[ROUGHTIME] %s\n", enable ? "Enabled" : "Disabled");
}

/**
 * Check if Roughtime is enabled
 */
bool roughtime_is_enabled(void) {
    return roughtime_enabled;
}

/**
 * Get request count
 */
uint32_t roughtime_get_requests(void) {
    return roughtime_requests;
}

/**
 * Get public key (for client configuration)
 */
const uint8_t *roughtime_get_pubkey(void) {
    return dummy_pubkey;
}
