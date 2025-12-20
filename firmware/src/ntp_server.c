/**
 * CHRONOS-Rb NTP Server Module
 * 
 * Implements NTPv4 server functionality for time distribution.
 * Provides Stratum 1 time when synchronized to rubidium reference.
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

#include "chronos_rb.h"

/*============================================================================
 * NTP CONSTANTS
 *============================================================================*/

/* NTP packet fields */
#define NTP_LI_NONE         0       /* No leap second warning */
#define NTP_LI_INSERT       1       /* Insert leap second */
#define NTP_LI_DELETE       2       /* Delete leap second */
#define NTP_LI_ALARM        3       /* Alarm - clock not synchronized */

#define NTP_MODE_CLIENT     3
#define NTP_MODE_SERVER     4

/* Reference ID for rubidium */
#define NTP_REFID_RBDM      0x5242444D  /* "RBDM" in big-endian */

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct udp_pcb *ntp_pcb = NULL;
static bool ntp_server_running = false;

/* Statistics */
static uint32_t ntp_requests_handled = 0;
static uint32_t ntp_errors = 0;
static uint64_t last_request_time = 0;

/*============================================================================
 * NTP PACKET HANDLING
 *============================================================================*/

/**
 * Build NTP response packet
 */
static void build_ntp_response(ntp_packet_t *request, ntp_packet_t *response,
                                timestamp_t *rx_time, timestamp_t *tx_time) {
    memset(response, 0, sizeof(ntp_packet_t));
    
    /* Determine leap indicator based on sync state */
    uint8_t li = NTP_LI_NONE;
    if (g_time_state.sync_state == SYNC_STATE_ERROR || 
        g_time_state.sync_state == SYNC_STATE_INIT) {
        li = NTP_LI_ALARM;
    }
    
    /* LI (2 bits) | VN (3 bits) | Mode (3 bits) */
    uint8_t vn = (request->li_vn_mode >> 3) & 0x07;  /* Use client's version */
    if (vn < 3) vn = 4;  /* Minimum version 3 */
    
    response->li_vn_mode = (li << 6) | (vn << 3) | NTP_MODE_SERVER;
    
    /* Stratum - 1 for primary reference (rubidium) */
    if (g_time_state.sync_state == SYNC_STATE_LOCKED) {
        response->stratum = NTP_STRATUM;
    } else if (g_time_state.sync_state >= SYNC_STATE_FINE) {
        response->stratum = NTP_STRATUM + 1;  /* Stratum 2 when not fully locked */
    } else {
        response->stratum = 16;  /* Unsynchronized */
    }
    
    /* Poll interval (use client's request) */
    response->poll = request->poll;
    if (response->poll < NTP_POLL_MIN) response->poll = NTP_POLL_MIN;
    if (response->poll > NTP_POLL_MAX) response->poll = NTP_POLL_MAX;
    
    /* Precision (2^precision seconds) - ~1µs = 2^-20 */
    response->precision = NTP_PRECISION;
    
    /* Root delay and dispersion */
    /* For a stratum 1 server, these are typically very small */
    response->root_delay = htonl(0);  /* No upstream delay */
    
    /* Root dispersion in NTP format (16.16 fixed point) */
    /* Based on our actual offset uncertainty */
    uint32_t dispersion_us = 10;  /* 10 microseconds typical */
    uint32_t dispersion_ntp = dispersion_us * 65536 / 1000000;
    response->root_dispersion = htonl(dispersion_ntp);
    
    /* Reference ID - "RBDM" for rubidium */
    response->ref_id = htonl(NTP_REFID_RBDM);
    
    /* Reference timestamp - time of last sync */
    timestamp_t ref_ts = get_current_time();
    /* Round to nearest second for reference timestamp */
    response->ref_ts_sec = htonl(ref_ts.seconds);
    response->ref_ts_frac = 0;
    
    /* Origin timestamp - copy from client's transmit timestamp */
    response->orig_ts_sec = request->tx_ts_sec;
    response->orig_ts_frac = request->tx_ts_frac;
    
    /* Receive timestamp - when we received the request */
    response->rx_ts_sec = htonl(rx_time->seconds);
    response->rx_ts_frac = htonl(rx_time->fraction);
    
    /* Transmit timestamp - current time */
    response->tx_ts_sec = htonl(tx_time->seconds);
    response->tx_ts_frac = htonl(tx_time->fraction);
}

/**
 * Handle incoming NTP request
 */
void ntp_handle_request(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, uint16_t port) {
    (void)arg;  /* Unused */
    
    /* Get receive timestamp immediately */
    timestamp_t rx_time = get_current_time();
    
    /* Validate packet size */
    if (p->tot_len < NTP_PACKET_SIZE) {
        printf("[NTP] Invalid packet size: %d\n", p->tot_len);
        ntp_errors++;
        g_stats.errors++;
        pbuf_free(p);
        return;
    }
    
    /* Parse request */
    ntp_packet_t request;
    pbuf_copy_partial(p, &request, NTP_PACKET_SIZE, 0);
    pbuf_free(p);
    
    /* Verify it's a client request */
    uint8_t mode = request.li_vn_mode & 0x07;
    if (mode != NTP_MODE_CLIENT) {
        /* Silently ignore non-client packets */
        return;
    }
    
    /* Get transmit timestamp */
    timestamp_t tx_time = get_current_time();
    
    /* Build response */
    ntp_packet_t response;
    build_ntp_response(&request, &response, &rx_time, &tx_time);
    
    /* Allocate pbuf for response */
    struct pbuf *resp_pbuf = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_SIZE, PBUF_RAM);
    if (resp_pbuf == NULL) {
        printf("[NTP] Failed to allocate response buffer\n");
        ntp_errors++;
        g_stats.errors++;
        return;
    }
    
    /* Copy response to pbuf */
    memcpy(resp_pbuf->payload, &response, NTP_PACKET_SIZE);
    
    /* Send response */
    err_t err = udp_sendto(pcb, resp_pbuf, addr, port);
    pbuf_free(resp_pbuf);
    
    if (err != ERR_OK) {
        printf("[NTP] Failed to send response: %d\n", err);
        ntp_errors++;
        g_stats.errors++;
        return;
    }
    
    /* Update statistics */
    ntp_requests_handled++;
    g_stats.ntp_requests++;
    last_request_time = time_us_64();
    
    /* Blink activity LED */
    led_blink_activity();
    
    /* Debug output (every 100 requests) */
    if (ntp_requests_handled % 100 == 0) {
        printf("[NTP] Handled %lu requests (stratum %d)\n", 
               ntp_requests_handled, response.stratum);
    }
}

/*============================================================================
 * INITIALIZATION AND TASK
 *============================================================================*/

/**
 * Initialize NTP server
 */
void ntp_server_init(void) {
    printf("[NTP] Initializing NTP server\n");
    
    /* Create UDP PCB */
    ntp_pcb = udp_new();
    if (ntp_pcb == NULL) {
        printf("[NTP] ERROR: Failed to create UDP PCB\n");
        return;
    }
    
    /* Bind to NTP port */
    err_t err = udp_bind(ntp_pcb, IP_ADDR_ANY, NTP_PORT);
    if (err != ERR_OK) {
        printf("[NTP] ERROR: Failed to bind to port %d: %d\n", NTP_PORT, err);
        udp_remove(ntp_pcb);
        ntp_pcb = NULL;
        return;
    }
    
    /* Set receive callback */
    udp_recv(ntp_pcb, ntp_handle_request, NULL);
    
    ntp_server_running = true;
    printf("[NTP] Server listening on port %d\n", NTP_PORT);
    printf("[NTP] Reference ID: RBDM (Rubidium)\n");
}

/**
 * NTP server task - call periodically
 */
void ntp_server_task(void) {
    /* Currently nothing needed here - all handling is in callbacks */
    /* Could add periodic cleanup or statistics logging */
}

/**
 * Check if NTP server is running
 */
bool ntp_server_is_running(void) {
    return ntp_server_running;
}

/**
 * Get NTP statistics
 */
void ntp_get_statistics(uint32_t *requests, uint32_t *errors) {
    *requests = ntp_requests_handled;
    *errors = ntp_errors;
}

/**
 * Shutdown NTP server
 */
void ntp_server_shutdown(void) {
    if (ntp_pcb != NULL) {
        udp_remove(ntp_pcb);
        ntp_pcb = NULL;
        ntp_server_running = false;
        printf("[NTP] Server stopped\n");
    }
}
