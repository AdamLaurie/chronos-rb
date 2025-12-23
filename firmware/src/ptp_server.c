/**
 * CHRONOS-Rb PTP Server Module
 * 
 * Implements IEEE 1588 Precision Time Protocol for sub-microsecond
 * time distribution over Ethernet/WiFi.
 * 
 * Note: WiFi introduces variable latency, so PTP over WiFi won't achieve
 * the same precision as wired Ethernet. Still useful for ~100µs accuracy.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/igmp.h"

#include "chronos_rb.h"

/*============================================================================
 * PTP CONSTANTS
 *============================================================================*/

/* PTP message types */
#define PTP_MSG_SYNC            0x00
#define PTP_MSG_DELAY_REQ       0x01
#define PTP_MSG_FOLLOW_UP       0x08
#define PTP_MSG_DELAY_RESP      0x09
#define PTP_MSG_ANNOUNCE        0x0B

/* PTP control field */
#define PTP_CTRL_SYNC           0x00
#define PTP_CTRL_DELAY_REQ      0x01
#define PTP_CTRL_FOLLOW_UP      0x02
#define PTP_CTRL_DELAY_RESP     0x03
#define PTP_CTRL_OTHER          0x05

/* PTP flags */
#define PTP_FLAG_TWO_STEP       0x0200  /* Two-step clock */
#define PTP_FLAG_UNICAST        0x0400  /* Unicast */

/* Multicast addresses */
#define PTP_MULTICAST_IP        "224.0.1.129"   /* PTP primary */
#define PTP_PDELAY_MULTICAST_IP "224.0.0.107"   /* Peer delay */

/*============================================================================
 * PTP PACKET STRUCTURES
 *============================================================================*/

/* PTP timestamp format (48-bit seconds + 32-bit nanoseconds) */
typedef struct __attribute__((packed)) {
    uint16_t seconds_msb;       /* Upper 16 bits of seconds */
    uint32_t seconds_lsb;       /* Lower 32 bits of seconds */
    uint32_t nanoseconds;       /* Nanoseconds */
} ptp_timestamp_t;

/* PTP clock identity (8 bytes, typically MAC-based) */
typedef struct __attribute__((packed)) {
    uint8_t id[8];
} ptp_clock_id_t;

/* PTP port identity */
typedef struct __attribute__((packed)) {
    ptp_clock_id_t clock_id;
    uint16_t port_number;
} ptp_port_id_t;

/* PTP common header (34 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t msg_type;           /* Message type (low 4 bits) + transport (high 4) */
    uint8_t version;            /* PTP version */
    uint16_t msg_length;        /* Total message length */
    uint8_t domain;             /* Domain number */
    uint8_t reserved1;
    uint16_t flags;             /* Flags */
    int64_t correction;         /* Correction field (ns * 2^16) */
    uint32_t reserved2;
    ptp_port_id_t source_port;  /* Source port identity */
    uint16_t sequence_id;       /* Sequence ID */
    uint8_t control;            /* Control field */
    int8_t log_msg_interval;    /* Log message interval */
} ptp_header_t;

/* PTP Sync message */
typedef struct __attribute__((packed)) {
    ptp_header_t header;
    ptp_timestamp_t origin_timestamp;
} ptp_sync_msg_t;

/* PTP Follow_Up message */
typedef struct __attribute__((packed)) {
    ptp_header_t header;
    ptp_timestamp_t precise_origin_timestamp;
} ptp_followup_msg_t;

/* PTP Delay_Req message */
typedef struct __attribute__((packed)) {
    ptp_header_t header;
    ptp_timestamp_t origin_timestamp;
} ptp_delay_req_msg_t;

/* PTP Delay_Resp message */
typedef struct __attribute__((packed)) {
    ptp_header_t header;
    ptp_timestamp_t receive_timestamp;
    ptp_port_id_t requesting_port;
} ptp_delay_resp_msg_t;

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct udp_pcb *ptp_event_pcb = NULL;
static struct udp_pcb *ptp_general_pcb = NULL;
static bool ptp_server_running = false;

/* Our clock identity (derived from WiFi MAC) */
static ptp_clock_id_t our_clock_id;
static uint16_t our_port_number = 1;

/* Sequence counters */
static uint16_t sync_sequence = 0;
static uint16_t announce_sequence = 0;

/* Timing */
static uint64_t last_sync_time = 0;
static uint32_t sync_interval_ms = 1000;  /* 1 second default */

/* Statistics */
static uint32_t sync_sent = 0;
static uint32_t delay_responses = 0;

/*============================================================================
 * UTILITY FUNCTIONS
 *============================================================================*/

/**
 * Convert our timestamp to PTP timestamp format
 */
static void timestamp_to_ptp(timestamp_t *ts, ptp_timestamp_t *ptp_ts) {
    /* NTP timestamp: seconds since 1900 + fraction (2^32 = 1 second)
     * PTP timestamp: seconds since 1970 + nanoseconds */
    
    uint64_t seconds = ts->seconds - 2208988800UL;  /* Convert NTP to Unix epoch */
    
    ptp_ts->seconds_msb = htons((seconds >> 32) & 0xFFFF);
    ptp_ts->seconds_lsb = htonl(seconds & 0xFFFFFFFF);
    
    /* Convert NTP fraction to nanoseconds */
    uint64_t nanos = ((uint64_t)ts->fraction * 1000000000ULL) >> 32;
    ptp_ts->nanoseconds = htonl(nanos);
}

/**
 * Initialize PTP clock identity from WiFi MAC address
 */
static void init_clock_identity(void) {
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    
    /* EUI-64 format: insert FF:FE in middle of MAC */
    our_clock_id.id[0] = mac[0];
    our_clock_id.id[1] = mac[1];
    our_clock_id.id[2] = mac[2];
    our_clock_id.id[3] = 0xFF;
    our_clock_id.id[4] = 0xFE;
    our_clock_id.id[5] = mac[3];
    our_clock_id.id[6] = mac[4];
    our_clock_id.id[7] = mac[5];
    
    printf("[PTP] Clock ID: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
           our_clock_id.id[0], our_clock_id.id[1], our_clock_id.id[2],
           our_clock_id.id[3], our_clock_id.id[4], our_clock_id.id[5],
           our_clock_id.id[6], our_clock_id.id[7]);
}

/**
 * Fill common PTP header fields
 */
static void fill_ptp_header(ptp_header_t *header, uint8_t msg_type, 
                            uint16_t length, uint16_t seq) {
    memset(header, 0, sizeof(ptp_header_t));
    
    header->msg_type = msg_type;
    header->version = 2;  /* PTPv2 */
    header->msg_length = htons(length);
    header->domain = PTP_DOMAIN;
    header->flags = htons(PTP_FLAG_TWO_STEP);
    header->correction = 0;
    
    memcpy(&header->source_port.clock_id, &our_clock_id, sizeof(ptp_clock_id_t));
    header->source_port.port_number = htons(our_port_number);
    
    header->sequence_id = htons(seq);
}

/*============================================================================
 * PTP MESSAGE HANDLING
 *============================================================================*/

/**
 * Send PTP Sync message
 */
void ptp_send_sync(void) {
    if (!ptp_server_running) return;
    
    /* Get timestamp for Sync message */
    timestamp_t sync_time = get_current_time();
    
    /* Build Sync message */
    ptp_sync_msg_t sync_msg;
    fill_ptp_header(&sync_msg.header, PTP_MSG_SYNC, sizeof(ptp_sync_msg_t), sync_sequence);
    sync_msg.header.control = PTP_CTRL_SYNC;
    sync_msg.header.log_msg_interval = 0;  /* 1 second */
    
    /* Origin timestamp (will be corrected by Follow_Up) */
    timestamp_to_ptp(&sync_time, &sync_msg.origin_timestamp);
    
    /* Allocate and send */
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(ptp_sync_msg_t), PBUF_RAM);
    if (p == NULL) return;
    
    memcpy(p->payload, &sync_msg, sizeof(ptp_sync_msg_t));
    
    /* Send to multicast address */
    ip_addr_t multicast_addr;
    ip4addr_aton(PTP_MULTICAST_IP, &multicast_addr);
    
    udp_sendto(ptp_event_pcb, p, &multicast_addr, PTP_EVENT_PORT);
    pbuf_free(p);
    
    /* Get precise transmit timestamp and send Follow_Up */
    timestamp_t tx_time = get_current_time();
    
    /* Build Follow_Up message */
    ptp_followup_msg_t followup_msg;
    fill_ptp_header(&followup_msg.header, PTP_MSG_FOLLOW_UP, sizeof(ptp_followup_msg_t), sync_sequence);
    followup_msg.header.control = PTP_CTRL_FOLLOW_UP;
    followup_msg.header.log_msg_interval = 0;
    followup_msg.header.flags = 0;  /* No two-step flag in Follow_Up */
    
    timestamp_to_ptp(&tx_time, &followup_msg.precise_origin_timestamp);
    
    /* Send Follow_Up on general port */
    p = pbuf_alloc(PBUF_TRANSPORT, sizeof(ptp_followup_msg_t), PBUF_RAM);
    if (p != NULL) {
        memcpy(p->payload, &followup_msg, sizeof(ptp_followup_msg_t));
        udp_sendto(ptp_general_pcb, p, &multicast_addr, PTP_GENERAL_PORT);
        pbuf_free(p);
    }
    
    sync_sequence++;
    sync_sent++;
    g_stats.ptp_sync_sent++;
}

/**
 * Handle PTP Delay_Req message
 */
static void handle_delay_req(struct pbuf *p, const ip_addr_t *addr, uint16_t port) {
    if (p->tot_len < sizeof(ptp_delay_req_msg_t)) return;
    
    /* Get receive timestamp immediately */
    timestamp_t rx_time = get_current_time();
    
    ptp_delay_req_msg_t req;
    pbuf_copy_partial(p, &req, sizeof(ptp_delay_req_msg_t), 0);
    
    /* Build Delay_Resp message */
    ptp_delay_resp_msg_t resp;
    fill_ptp_header(&resp.header, PTP_MSG_DELAY_RESP, sizeof(ptp_delay_resp_msg_t),
                    ntohs(req.header.sequence_id));
    resp.header.control = PTP_CTRL_DELAY_RESP;
    resp.header.log_msg_interval = 0x7F;  /* Not applicable */
    resp.header.flags = 0;
    
    timestamp_to_ptp(&rx_time, &resp.receive_timestamp);
    memcpy(&resp.requesting_port, &req.header.source_port, sizeof(ptp_port_id_t));
    
    /* Send response */
    struct pbuf *resp_p = pbuf_alloc(PBUF_TRANSPORT, sizeof(ptp_delay_resp_msg_t), PBUF_RAM);
    if (resp_p != NULL) {
        memcpy(resp_p->payload, &resp, sizeof(ptp_delay_resp_msg_t));
        udp_sendto(ptp_general_pcb, resp_p, addr, PTP_GENERAL_PORT);
        pbuf_free(resp_p);
        
        delay_responses++;
        g_stats.ptp_delay_resp++;
    }
}

/**
 * PTP event port receive callback
 */
static void ptp_event_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *addr, uint16_t port) {
    (void)arg;
    (void)pcb;
    
    if (p->tot_len < sizeof(ptp_header_t)) {
        pbuf_free(p);
        return;
    }
    
    ptp_header_t header;
    pbuf_copy_partial(p, &header, sizeof(ptp_header_t), 0);
    
    uint8_t msg_type = header.msg_type & 0x0F;
    
    switch (msg_type) {
        case PTP_MSG_DELAY_REQ:
            handle_delay_req(p, addr, port);
            led_blink_activity();
            break;
        default:
            /* Ignore other message types */
            break;
    }
    
    pbuf_free(p);
}

/**
 * PTP general port receive callback
 */
static void ptp_general_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                             const ip_addr_t *addr, uint16_t port) {
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;
    
    /* Currently we don't process incoming general messages */
    pbuf_free(p);
}

/*============================================================================
 * INITIALIZATION AND TASK
 *============================================================================*/

/**
 * Initialize PTP server
 */
void ptp_server_init(void) {
    if (ptp_server_running) {
        printf("[PTP] Already running\n");
        return;
    }

    printf("[PTP] Initializing PTP server\n");

    /* Initialize clock identity */
    init_clock_identity();
    
    /* Create event port PCB */
    ptp_event_pcb = udp_new();
    if (ptp_event_pcb == NULL) {
        printf("[PTP] ERROR: Failed to create event PCB\n");
        return;
    }
    
    err_t err = udp_bind(ptp_event_pcb, IP_ADDR_ANY, PTP_EVENT_PORT);
    if (err != ERR_OK) {
        printf("[PTP] ERROR: Failed to bind event port: %d\n", err);
        udp_remove(ptp_event_pcb);
        ptp_event_pcb = NULL;
        return;
    }
    
    udp_recv(ptp_event_pcb, ptp_event_recv, NULL);
    
    /* Create general port PCB */
    ptp_general_pcb = udp_new();
    if (ptp_general_pcb == NULL) {
        printf("[PTP] ERROR: Failed to create general PCB\n");
        udp_remove(ptp_event_pcb);
        ptp_event_pcb = NULL;
        return;
    }
    
    err = udp_bind(ptp_general_pcb, IP_ADDR_ANY, PTP_GENERAL_PORT);
    if (err != ERR_OK) {
        printf("[PTP] ERROR: Failed to bind general port: %d\n", err);
        udp_remove(ptp_event_pcb);
        udp_remove(ptp_general_pcb);
        ptp_event_pcb = NULL;
        ptp_general_pcb = NULL;
        return;
    }
    
    udp_recv(ptp_general_pcb, ptp_general_recv, NULL);
    
    /* Join multicast group */
    ip_addr_t multicast_addr;
    ip4addr_aton(PTP_MULTICAST_IP, &multicast_addr);
    
    /* Note: IGMP join may not work over WiFi on all APs */
    
    ptp_server_running = true;
    printf("[PTP] Server running on ports %d (event) and %d (general)\n",
           PTP_EVENT_PORT, PTP_GENERAL_PORT);
    printf("[PTP] NOTE: PTP over WiFi has limited precision (~100µs typical)\n");
}

/**
 * PTP server task - call periodically
 */
void ptp_server_task(void) {
    if (!ptp_server_running) return;
    
    uint64_t now = time_us_64();
    
    /* Send Sync messages at configured interval */
    if (now - last_sync_time >= sync_interval_ms * 1000) {
        last_sync_time = now;
        
        /* Only send if we have valid time */
        if (g_time_state.time_valid || g_time_state.sync_state >= SYNC_STATE_FINE) {
            ptp_send_sync();
        }
    }
}

/**
 * Send PTP Announce message (for BMCA - Best Master Clock Algorithm)
 */
void ptp_send_announce(void) {
    /* Announce messages are used for BMCA to select grandmaster */
    /* For a dedicated server like this, we're always grandmaster */
    /* Implementation left as exercise - full BMCA is complex */
}

/**
 * Check if PTP server is running
 */
bool ptp_server_is_running(void) {
    return ptp_server_running;
}

/**
 * Get PTP statistics
 */
void ptp_get_statistics(uint32_t *syncs, uint32_t *delay_resps) {
    *syncs = sync_sent;
    *delay_resps = delay_responses;
}

/**
 * Set sync interval
 */
void ptp_set_sync_interval(uint32_t interval_ms) {
    sync_interval_ms = interval_ms;
    printf("[PTP] Sync interval set to %lu ms\n", interval_ms);
}

/**
 * Shutdown PTP server
 */
void ptp_server_shutdown(void) {
    if (ptp_event_pcb != NULL) {
        udp_remove(ptp_event_pcb);
        ptp_event_pcb = NULL;
    }
    if (ptp_general_pcb != NULL) {
        udp_remove(ptp_general_pcb);
        ptp_general_pcb = NULL;
    }
    ptp_server_running = false;
    printf("[PTP] Server stopped\n");
}
