/**
 * CHRONOS-Rb Legacy Time Protocols
 *
 * TIME Protocol (RFC 868) - UDP/TCP port 37
 *   Returns 32-bit seconds since 1900-01-01 00:00:00 UTC
 *
 * Daytime Protocol (RFC 867) - TCP port 13
 *   Returns human-readable ASCII timestamp
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"

#include "chronos_rb.h"
#include "time_protocol.h"

/*============================================================================
 * CONFIGURATION
 *============================================================================*/

#define TIME_PORT       37
#define DAYTIME_PORT    13

/* NTP epoch is 1900, Unix epoch is 1970 */
#define NTP_UNIX_OFFSET 2208988800UL

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct udp_pcb *time_udp_pcb = NULL;
static struct tcp_pcb *daytime_tcp_pcb = NULL;

static uint32_t time_requests = 0;
static uint32_t daytime_requests = 0;

/*============================================================================
 * TIME PROTOCOL (RFC 868)
 *============================================================================*/

/**
 * Get current time as NTP timestamp (seconds since 1900)
 */
static uint32_t get_ntp_seconds(void) {
    timestamp_t ts = get_current_time();
    return ts.seconds;
}

/**
 * UDP receive callback for TIME protocol
 */
static void time_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port) {
    (void)arg;

    if (p != NULL) {
        pbuf_free(p);  /* Don't need the incoming data */

        /* Get current time */
        uint32_t ntp_time = get_ntp_seconds();

        /* Convert to network byte order (big endian) */
        uint32_t time_be = lwip_htonl(ntp_time);

        /* Create response pbuf */
        struct pbuf *resp = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);
        if (resp != NULL) {
            memcpy(resp->payload, &time_be, 4);
            udp_sendto(pcb, resp, addr, port);
            pbuf_free(resp);
            time_requests++;
        }
    }
}

/**
 * Initialize TIME protocol server (UDP port 37)
 */
static bool time_protocol_init(void) {
    time_udp_pcb = udp_new();
    if (time_udp_pcb == NULL) {
        printf("[TIME] Failed to create UDP PCB\n");
        return false;
    }

    err_t err = udp_bind(time_udp_pcb, IP_ADDR_ANY, TIME_PORT);
    if (err != ERR_OK) {
        printf("[TIME] Failed to bind UDP port %d: %d\n", TIME_PORT, err);
        udp_remove(time_udp_pcb);
        time_udp_pcb = NULL;
        return false;
    }

    udp_recv(time_udp_pcb, time_udp_recv, NULL);
    printf("[TIME] RFC 868 server listening on UDP port %d\n", TIME_PORT);
    return true;
}

/*============================================================================
 * DAYTIME PROTOCOL (RFC 867)
 *============================================================================*/

/* Day names for daytime format */
static const char *day_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* Month names for daytime format */
static const char *month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * Convert NTP timestamp to broken-down time
 */
static void ntp_to_tm(uint32_t ntp_secs, int *year, int *month, int *day,
                      int *hour, int *min, int *sec, int *wday) {
    /* Convert NTP to Unix time */
    uint32_t unix_time = ntp_secs - NTP_UNIX_OFFSET;

    /* Days since epoch */
    uint32_t days = unix_time / 86400;
    uint32_t remaining = unix_time % 86400;

    *hour = remaining / 3600;
    *min = (remaining % 3600) / 60;
    *sec = remaining % 60;

    /* Day of week (Jan 1, 1970 was Thursday = 4) */
    *wday = (days + 4) % 7;

    /* Calculate year, month, day */
    int y = 1970;
    while (1) {
        int days_in_year = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (days < (uint32_t)days_in_year) break;
        days -= days_in_year;
        y++;
    }
    *year = y;

    /* Days in each month */
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) {
        days_in_month[1] = 29;  /* Leap year */
    }

    int m = 0;
    while (days >= (uint32_t)days_in_month[m]) {
        days -= days_in_month[m];
        m++;
    }
    *month = m;
    *day = days + 1;
}

/**
 * Format daytime string per RFC 867
 * Format: "Weekday, Month DD, YYYY HH:MM:SS-Zone\r\n"
 */
static int format_daytime(char *buf, size_t len) {
    uint32_t ntp_secs = get_ntp_seconds();

    int year, month, day, hour, min, sec, wday;
    ntp_to_tm(ntp_secs, &year, &month, &day, &hour, &min, &sec, &wday);

    return snprintf(buf, len, "%s, %s %02d, %04d %02d:%02d:%02d-UTC\r\n",
                    day_names[wday], month_names[month], day, year,
                    hour, min, sec);
}

/**
 * TCP connection callback - send daytime and close
 */
static err_t daytime_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    /* Format daytime response */
    char daytime_str[64];
    int len = format_daytime(daytime_str, sizeof(daytime_str));

    /* Send response */
    tcp_write(newpcb, daytime_str, len, TCP_WRITE_FLAG_COPY);
    tcp_output(newpcb);

    /* Close connection */
    tcp_close(newpcb);

    daytime_requests++;
    return ERR_OK;
}

/**
 * Initialize Daytime protocol server (TCP port 13)
 */
static bool daytime_protocol_init(void) {
    daytime_tcp_pcb = tcp_new();
    if (daytime_tcp_pcb == NULL) {
        printf("[DAYTIME] Failed to create TCP PCB\n");
        return false;
    }

    err_t err = tcp_bind(daytime_tcp_pcb, IP_ADDR_ANY, DAYTIME_PORT);
    if (err != ERR_OK) {
        printf("[DAYTIME] Failed to bind TCP port %d: %d\n", DAYTIME_PORT, err);
        tcp_close(daytime_tcp_pcb);
        daytime_tcp_pcb = NULL;
        return false;
    }

    daytime_tcp_pcb = tcp_listen(daytime_tcp_pcb);
    if (daytime_tcp_pcb == NULL) {
        printf("[DAYTIME] Failed to listen\n");
        return false;
    }

    tcp_accept(daytime_tcp_pcb, daytime_tcp_accept);
    printf("[DAYTIME] RFC 867 server listening on TCP port %d\n", DAYTIME_PORT);
    return true;
}

/*============================================================================
 * PUBLIC API
 *============================================================================*/

/**
 * Initialize all legacy time protocols
 */
void time_protocols_init(void) {
    printf("[TIME] Initializing legacy time protocols\n");
    time_protocol_init();
    daytime_protocol_init();
}

/**
 * Get request statistics
 */
void time_protocols_get_stats(uint32_t *time_reqs, uint32_t *daytime_reqs) {
    if (time_reqs) *time_reqs = time_requests;
    if (daytime_reqs) *daytime_reqs = daytime_requests;
}
