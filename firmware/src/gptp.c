/**
 * CHRONOS-Rb gPTP (IEEE 802.1AS) Support
 *
 * IEEE 802.1AS is a profile of IEEE 1588 PTP optimized for
 * Time-Sensitive Networking (TSN). Key characteristics:
 *
 * - Uses peer-delay mechanism only (not end-to-end)
 * - Multicast communication (224.0.1.129)
 * - Strict timing requirements
 * - Automotive and industrial applications
 *
 * This module enhances the existing PTP server with gPTP extensions:
 * - Peer delay request/response handling
 * - gPTP-specific TLV extensions
 * - Follow_Up with correction field
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"

#include "chronos_rb.h"
#include "gptp.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

/* gPTP uses same ports as PTP */
#define GPTP_EVENT_PORT     319
#define GPTP_GENERAL_PORT   320

/* gPTP multicast address: 224.0.1.129 (same as PTP) */
#define GPTP_MCAST_ADDR     "224.0.1.129"

/* gPTP-specific constants */
#define GPTP_DOMAIN         0
#define GPTP_VERSION        2

/* Message types */
#define MSG_SYNC            0x00
#define MSG_DELAY_REQ       0x01
#define MSG_PDELAY_REQ      0x02
#define MSG_PDELAY_RESP     0x03
#define MSG_FOLLOW_UP       0x08
#define MSG_DELAY_RESP      0x09
#define MSG_PDELAY_RESP_FU  0x0A
#define MSG_ANNOUNCE        0x0B

/* TLV types for gPTP */
#define TLV_PATH_TRACE      0x0008
#define TLV_ORGANIZATION    0x0003

/* gPTP timing intervals */
#define PDELAY_REQ_INTERVAL_MS  1000    /* 1 second */
#define SYNC_INTERVAL_MS        1000    /* 1 Hz for now to reduce load */

/*============================================================================
 * PTP HEADER STRUCTURE
 *============================================================================*/

typedef struct __attribute__((packed)) {
    uint8_t  message_type;
    uint8_t  version_ptp;
    uint16_t message_length;
    uint8_t  domain_number;
    uint8_t  reserved1;
    uint16_t flags;
    int64_t  correction_field;
    uint32_t reserved2;
    uint8_t  source_port_identity[10];
    uint16_t sequence_id;
    uint8_t  control_field;
    int8_t   log_message_interval;
} ptp_header_t;

typedef struct __attribute__((packed)) {
    uint16_t seconds_msb;
    uint32_t seconds_lsb;
    uint32_t nanoseconds;
} ptp_timestamp_t;

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct udp_pcb *gptp_event_pcb = NULL;
static struct udp_pcb *gptp_general_pcb = NULL;

static bool gptp_enabled = false;
static uint16_t pdelay_sequence = 0;
static uint16_t sync_sequence = 0;

/* Peer delay measurement */
static uint64_t pdelay_t1 = 0;  /* Pdelay_Req transmit time */
static uint64_t pdelay_t2 = 0;  /* Pdelay_Req receive time (at peer) */
static uint64_t pdelay_t3 = 0;  /* Pdelay_Resp transmit time (at peer) */
static uint64_t pdelay_t4 = 0;  /* Pdelay_Resp receive time */
static int64_t peer_delay_ns = 0;

/* Statistics */
static uint32_t gptp_sync_sent = 0;
static uint32_t gptp_pdelay_req_recv = 0;
static uint32_t gptp_pdelay_resp_sent = 0;

/* Clock identity (derived from MAC address) */
static uint8_t clock_identity[8] = {0};

/* Last sync/pdelay time */
static uint32_t last_sync_ms = 0;

/*============================================================================
 * HELPER FUNCTIONS
 *============================================================================*/

/**
 * Get current time as PTP timestamp
 */
static void get_ptp_timestamp(ptp_timestamp_t *ts) {
    timestamp_t t = get_current_time();

    /* NTP to PTP epoch adjustment (NTP: 1900, PTP: 1970) */
    uint32_t ptp_secs = t.seconds - 2208988800UL;

    ts->seconds_msb = 0;  /* Assume < 2036 */
    ts->seconds_lsb = lwip_htonl(ptp_secs);

    /* Convert fraction to nanoseconds */
    uint64_t frac_ns = ((uint64_t)t.fraction * 1000000000ULL) >> 32;
    ts->nanoseconds = lwip_htonl((uint32_t)frac_ns);
}

/**
 * Get time in nanoseconds since epoch (for delay calculation)
 */
static uint64_t get_time_ns(void) {
    timestamp_t t = get_current_time();
    uint64_t secs = t.seconds - 2208988800UL;
    uint64_t frac_ns = ((uint64_t)t.fraction * 1000000000ULL) >> 32;
    return secs * 1000000000ULL + frac_ns;
}

/**
 * Initialize clock identity from Pico's unique ID
 */
static void init_clock_identity(void) {
    /* Use Pico's unique board ID as basis */
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    /* EUI-64 format: insert FF:FE in middle of 6-byte ID */
    clock_identity[0] = board_id.id[0];
    clock_identity[1] = board_id.id[1];
    clock_identity[2] = board_id.id[2];
    clock_identity[3] = 0xFF;
    clock_identity[4] = 0xFE;
    clock_identity[5] = board_id.id[3];
    clock_identity[6] = board_id.id[4];
    clock_identity[7] = board_id.id[5];
}

/*============================================================================
 * MESSAGE BUILDING
 *============================================================================*/

/**
 * Build PTP header
 */
static void build_ptp_header(ptp_header_t *hdr, uint8_t msg_type,
                             uint16_t length, uint16_t seq_id,
                             int8_t log_interval) {
    memset(hdr, 0, sizeof(*hdr));

    hdr->message_type = msg_type;
    hdr->version_ptp = GPTP_VERSION;
    hdr->message_length = lwip_htons(length);
    hdr->domain_number = GPTP_DOMAIN;
    hdr->flags = lwip_htons(0x0200);  /* Two-step flag */
    hdr->correction_field = 0;

    memcpy(hdr->source_port_identity, clock_identity, 8);
    hdr->source_port_identity[8] = 0;
    hdr->source_port_identity[9] = 1;  /* Port number 1 */

    hdr->sequence_id = lwip_htons(seq_id);
    hdr->log_message_interval = log_interval;

    /* Control field based on message type */
    switch (msg_type) {
        case MSG_SYNC:         hdr->control_field = 0x00; break;
        case MSG_DELAY_REQ:    hdr->control_field = 0x01; break;
        case MSG_FOLLOW_UP:    hdr->control_field = 0x02; break;
        case MSG_DELAY_RESP:   hdr->control_field = 0x03; break;
        case MSG_PDELAY_REQ:   hdr->control_field = 0x05; break;
        case MSG_PDELAY_RESP:  hdr->control_field = 0x05; break;
        default:               hdr->control_field = 0x05; break;
    }
}

/**
 * Send Sync message
 */
static void send_sync(void) {
    uint8_t msg[44];
    ptp_header_t *hdr = (ptp_header_t *)msg;

    build_ptp_header(hdr, MSG_SYNC, 44, sync_sequence, -3);  /* 8 Hz */

    /* Origin timestamp (will be in Follow_Up) */
    ptp_timestamp_t *ts = (ptp_timestamp_t *)(msg + 34);
    memset(ts, 0, sizeof(*ts));

    /* Send to multicast */
    ip_addr_t mcast;
    ipaddr_aton(GPTP_MCAST_ADDR, &mcast);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 44, PBUF_RAM);
    if (p) {
        memcpy(p->payload, msg, 44);
        udp_sendto(gptp_event_pcb, p, &mcast, GPTP_EVENT_PORT);
        pbuf_free(p);
        gptp_sync_sent++;
    }

    /* Send Follow_Up with actual timestamp */
    uint8_t fu_msg[44];
    ptp_header_t *fu_hdr = (ptp_header_t *)fu_msg;

    build_ptp_header(fu_hdr, MSG_FOLLOW_UP, 44, sync_sequence, -3);
    fu_hdr->flags = lwip_htons(0x0000);  /* No two-step for follow-up */

    ptp_timestamp_t *fu_ts = (ptp_timestamp_t *)(fu_msg + 34);
    get_ptp_timestamp(fu_ts);

    struct pbuf *fu_p = pbuf_alloc(PBUF_TRANSPORT, 44, PBUF_RAM);
    if (fu_p) {
        memcpy(fu_p->payload, fu_msg, 44);
        udp_sendto(gptp_general_pcb, fu_p, &mcast, GPTP_GENERAL_PORT);
        pbuf_free(fu_p);
    }

    sync_sequence++;
}

/**
 * Handle Pdelay_Req - respond with Pdelay_Resp and Pdelay_Resp_Follow_Up
 */
static void handle_pdelay_req(const uint8_t *data, size_t len,
                              const ip_addr_t *src_addr, u16_t src_port) {
    if (len < 44) return;

    ptp_header_t *req_hdr = (ptp_header_t *)data;
    uint64_t t2 = get_time_ns();  /* Reception time */

    gptp_pdelay_req_recv++;

    /* Build Pdelay_Resp */
    uint8_t resp[54];
    ptp_header_t *resp_hdr = (ptp_header_t *)resp;

    build_ptp_header(resp_hdr, MSG_PDELAY_RESP, 54,
                     lwip_ntohs(req_hdr->sequence_id), 0);

    /* Request receipt timestamp (t2) */
    ptp_timestamp_t *ts = (ptp_timestamp_t *)(resp + 34);
    ts->seconds_msb = 0;
    ts->seconds_lsb = lwip_htonl((uint32_t)(t2 / 1000000000ULL));
    ts->nanoseconds = lwip_htonl((uint32_t)(t2 % 1000000000ULL));

    /* Requesting port identity */
    memcpy(resp + 44, req_hdr->source_port_identity, 10);

    /* Send response */
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 54, PBUF_RAM);
    if (p) {
        memcpy(p->payload, resp, 54);
        udp_sendto(gptp_event_pcb, p, src_addr, src_port);
        pbuf_free(p);
        gptp_pdelay_resp_sent++;
    }

    /* Send Pdelay_Resp_Follow_Up with t3 (response transmit time) */
    uint64_t t3 = get_time_ns();

    uint8_t fu[54];
    ptp_header_t *fu_hdr = (ptp_header_t *)fu;

    build_ptp_header(fu_hdr, MSG_PDELAY_RESP_FU, 54,
                     lwip_ntohs(req_hdr->sequence_id), 0);

    ptp_timestamp_t *fu_ts = (ptp_timestamp_t *)(fu + 34);
    fu_ts->seconds_msb = 0;
    fu_ts->seconds_lsb = lwip_htonl((uint32_t)(t3 / 1000000000ULL));
    fu_ts->nanoseconds = lwip_htonl((uint32_t)(t3 % 1000000000ULL));

    memcpy(fu + 44, req_hdr->source_port_identity, 10);

    struct pbuf *fu_p = pbuf_alloc(PBUF_TRANSPORT, 54, PBUF_RAM);
    if (fu_p) {
        memcpy(fu_p->payload, fu, 54);
        udp_sendto(gptp_general_pcb, fu_p, src_addr, GPTP_GENERAL_PORT);
        pbuf_free(fu_p);
    }
}

/*============================================================================
 * UDP CALLBACKS
 *============================================================================*/

/**
 * Event port receive callback (port 319)
 */
static void gptp_event_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;

    if (p == NULL || !gptp_enabled) {
        if (p) pbuf_free(p);
        return;
    }

    if (p->tot_len >= sizeof(ptp_header_t)) {
        ptp_header_t *hdr = (ptp_header_t *)p->payload;
        uint8_t msg_type = hdr->message_type & 0x0F;

        switch (msg_type) {
            case MSG_PDELAY_REQ:
                handle_pdelay_req((uint8_t *)p->payload, p->tot_len, addr, port);
                break;
            case MSG_DELAY_REQ:
                /* Handle as regular PTP delay request */
                /* (Could forward to ptp_server.c) */
                break;
        }
    }

    pbuf_free(p);
}

/**
 * General port receive callback (port 320)
 */
static void gptp_general_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;

    if (p) pbuf_free(p);
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize gPTP
 * Note: gPTP shares ports with PTP (319/320). This module adds gPTP-specific
 * extensions but doesn't create separate sockets to avoid conflicts.
 */
void gptp_init(void) {
    printf("[gPTP] Initializing IEEE 802.1AS support\n");

    init_clock_identity();

    /* Note: We don't bind to ports here because PTP server already has them.
     * gPTP mode adds peer delay and faster sync to existing PTP.
     * The gPTP_task() sends Sync messages independently. */

    /* For now, create unbound PCBs for sending only */
    gptp_event_pcb = udp_new();
    gptp_general_pcb = udp_new();

    if (!gptp_event_pcb || !gptp_general_pcb) {
        printf("[gPTP] Failed to create UDP PCBs\n");
        gptp_enabled = false;
        return;
    }

    printf("[gPTP] Clock ID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
           clock_identity[0], clock_identity[1], clock_identity[2],
           clock_identity[3], clock_identity[4], clock_identity[5],
           clock_identity[6], clock_identity[7]);

    gptp_enabled = true;
    printf("[gPTP] Initialized (shares ports with PTP)\n");
}

/**
 * gPTP task - call from main loop
 */
void gptp_task(void) {
    if (!gptp_enabled) return;

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    /* Send Sync at 8 Hz (every 125ms) */
    if (now_ms - last_sync_ms >= SYNC_INTERVAL_MS) {
        last_sync_ms = now_ms;
        send_sync();
    }
}

/**
 * Enable/disable gPTP
 */
void gptp_enable(bool enable) {
    gptp_enabled = enable;
    printf("[gPTP] %s\n", enable ? "Enabled" : "Disabled");
}

/**
 * Check if gPTP is enabled
 */
bool gptp_is_enabled(void) {
    return gptp_enabled;
}

/**
 * Get peer delay in nanoseconds
 */
int64_t gptp_get_peer_delay(void) {
    return peer_delay_ns;
}

/**
 * Get statistics
 */
void gptp_get_stats(uint32_t *sync_sent, uint32_t *pdelay_req,
                    uint32_t *pdelay_resp) {
    if (sync_sent) *sync_sent = gptp_sync_sent;
    if (pdelay_req) *pdelay_req = gptp_pdelay_req_recv;
    if (pdelay_resp) *pdelay_resp = gptp_pdelay_resp_sent;
}
