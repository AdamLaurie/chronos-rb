/**
 * CHRONOS-Rb Web Interface Module
 * 
 * Provides a web-based configuration and monitoring interface.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "lwip/tcp.h"

#include "chronos_rb.h"

/*============================================================================
 * HTTP CONSTANTS
 *============================================================================*/

#define HTTP_RESPONSE_HEADER \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: text/html; charset=utf-8\r\n" \
    "Connection: close\r\n" \
    "Cache-Control: no-cache\r\n" \
    "\r\n"

#define HTTP_JSON_HEADER \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: application/json\r\n" \
    "Connection: close\r\n" \
    "Access-Control-Allow-Origin: *\r\n" \
    "\r\n"

#define HTTP_404_RESPONSE \
    "HTTP/1.1 404 Not Found\r\n" \
    "Content-Type: text/plain\r\n" \
    "Connection: close\r\n" \
    "\r\n" \
    "404 Not Found"

/*============================================================================
 * HTML CONTENT
 *============================================================================*/

static const char HTML_PAGE[] = 
"<!DOCTYPE html>"
"<html><head>"
"<title>CHRONOS-Rb Time Server</title>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 100%%);color:#eee;min-height:100vh;padding:20px}"
".container{max-width:800px;margin:0 auto}"
"h1{text-align:center;margin-bottom:30px;font-size:2em;"
"background:linear-gradient(90deg,#e94560,#0f3460);-webkit-background-clip:text;"
"-webkit-text-fill-color:transparent}"
".card{background:rgba(255,255,255,0.05);border-radius:15px;padding:20px;margin-bottom:20px;"
"backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}"
".card h2{color:#e94560;margin-bottom:15px;font-size:1.2em}"
".stat{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid rgba(255,255,255,0.1)}"
".stat:last-child{border-bottom:none}"
".stat-label{color:#aaa}"
".stat-value{font-weight:bold;font-family:'Courier New',monospace}"
".status-locked{color:#4ade80}"
".status-syncing{color:#fbbf24}"
".status-error{color:#f87171}"
".led{display:inline-block;width:12px;height:12px;border-radius:50%%;margin-right:8px}"
".led-green{background:#4ade80;box-shadow:0 0 10px #4ade80}"
".led-yellow{background:#fbbf24;box-shadow:0 0 10px #fbbf24}"
".led-red{background:#f87171;box-shadow:0 0 10px #f87171}"
".led-off{background:#333}"
"footer{text-align:center;margin-top:30px;color:#666;font-size:0.9em}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>⚛ CHRONOS-Rb</h1>"
"<div class='card'>"
"<h2>System Status</h2>"
"<div class='stat'><span class='stat-label'>Sync State</span>"
"<span class='stat-value %s'><span class='led %s'></span>%s</span></div>"
"<div class='stat'><span class='stat-label'>Rubidium Lock</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Time Valid</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Uptime</span>"
"<span class='stat-value'>%lu seconds</span></div>"
"</div>"
"<div class='card'>"
"<h2>Time Discipline</h2>"
"<div class='stat'><span class='stat-label'>Offset</span>"
"<span class='stat-value'>%lld ns</span></div>"
"<div class='stat'><span class='stat-label'>Frequency Offset</span>"
"<span class='stat-value'>%.3f ppb</span></div>"
"<div class='stat'><span class='stat-label'>PPS Count</span>"
"<span class='stat-value'>%lu</span></div>"
"<div class='stat'><span class='stat-label'>Last Freq Count</span>"
"<span class='stat-value'>%lu Hz</span></div>"
"</div>"
"<div class='card'>"
"<h2>Network Services</h2>"
"<div class='stat'><span class='stat-label'>IP Address</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>NTP Port</span>"
"<span class='stat-value'>%d (Stratum %d)</span></div>"
"<div class='stat'><span class='stat-label'>NTP Requests</span>"
"<span class='stat-value'>%lu</span></div>"
"<div class='stat'><span class='stat-label'>PTP Sync Sent</span>"
"<span class='stat-value'>%lu</span></div>"
"</div>"
"<div class='card'>"
"<h2>NTP Client Configuration</h2>"
"<div class='stat'><span class='stat-label'>Linux/Mac</span>"
"<span class='stat-value'>ntpdate %s</span></div>"
"<div class='stat'><span class='stat-label'>Windows</span>"
"<span class='stat-value'>w32tm /stripchart /computer:%s</span></div>"
"</div>"
"<footer>CHRONOS-Rb v%s | Rubidium Disciplined NTP/PTP Server</footer>"
"</div>"
"<script>setTimeout(()=>location.reload(),5000)</script>"
"</body></html>";

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct tcp_pcb *web_pcb = NULL;
static bool web_running = false;

/*============================================================================
 * HTTP HANDLERS
 *============================================================================*/

/**
 * Generate status page HTML
 */
static int generate_status_page(char *buf, size_t len) {
    const char *sync_states[] = {
        "INIT", "FREQ_CAL", "COARSE", "FINE", "LOCKED", "HOLDOVER", "ERROR"
    };
    
    const char *sync_class = "status-syncing";
    const char *led_class = "led-yellow";
    
    if (g_time_state.sync_state == SYNC_STATE_LOCKED) {
        sync_class = "status-locked";
        led_class = "led-green";
    } else if (g_time_state.sync_state == SYNC_STATE_ERROR) {
        sync_class = "status-error";
        led_class = "led-red";
    }
    
    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));
    
    uint8_t stratum = NTP_STRATUM;
    if (g_time_state.sync_state != SYNC_STATE_LOCKED) {
        stratum = (g_time_state.sync_state >= SYNC_STATE_FINE) ? NTP_STRATUM + 1 : 16;
    }
    
    return snprintf(buf, len, HTML_PAGE,
        sync_class, led_class, sync_states[g_time_state.sync_state],
        g_time_state.rb_locked ? "LOCKED" : "UNLOCKED",
        g_time_state.time_valid ? "YES" : "NO",
        (unsigned long)(time_us_64() / 1000000),
        (long long)g_time_state.offset_ns,
        (double)g_time_state.frequency_offset,
        (unsigned long)g_time_state.pps_count,
        (unsigned long)g_time_state.last_freq_count,
        ip_str,
        NTP_PORT, (int)stratum,
        (unsigned long)g_stats.ntp_requests,
        (unsigned long)g_stats.ptp_sync_sent,
        ip_str, ip_str,
        CHRONOS_VERSION_STRING
    );
}

/**
 * Generate JSON status
 */
static int generate_json_status(char *buf, size_t len) {
    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));
    
    return snprintf(buf, len,
        "{"
        "\"sync_state\":%d,"
        "\"rb_locked\":%s,"
        "\"time_valid\":%s,"
        "\"offset_ns\":%lld,"
        "\"freq_offset_ppb\":%.3f,"
        "\"pps_count\":%lu,"
        "\"freq_count\":%lu,"
        "\"ntp_requests\":%lu,"
        "\"ptp_syncs\":%lu,"
        "\"ip\":\"%s\","
        "\"version\":\"%s\""
        "}",
        g_time_state.sync_state,
        g_time_state.rb_locked ? "true" : "false",
        g_time_state.time_valid ? "true" : "false",
        g_time_state.offset_ns,
        g_time_state.frequency_offset,
        g_time_state.pps_count,
        g_time_state.last_freq_count,
        g_stats.ntp_requests,
        g_stats.ptp_sync_sent,
        ip_str,
        CHRONOS_VERSION_STRING
    );
}

/*============================================================================
 * TCP CALLBACKS
 *============================================================================*/

static err_t web_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)arg;
    (void)len;
    tcp_close(tpcb);
    return ERR_OK;
}

static err_t web_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg;
    
    if (p == NULL || err != ERR_OK) {
        if (p != NULL) pbuf_free(p);
        tcp_close(tpcb);
        return ERR_OK;
    }
    
    tcp_recved(tpcb, p->tot_len);
    
    /* Parse HTTP request (simple parsing) */
    char *request = (char *)p->payload;
    bool is_json = (strstr(request, "/api/status") != NULL);
    
    /* Allocate response buffer */
    static char response[8192];
    int content_len;

    if (is_json) {
        char json_buf[512];
        content_len = generate_json_status(json_buf, sizeof(json_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_JSON_HEADER, json_buf);
    } else if (strstr(request, "GET / ") != NULL || strstr(request, "GET /index") != NULL) {
        static char html_buf[6000];
        generate_status_page(html_buf, sizeof(html_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);
    } else {
        strcpy(response, HTTP_404_RESPONSE);
    }
    
    pbuf_free(p);
    
    /* Send response */
    size_t resp_len = strlen(response);
    err_t write_err = tcp_write(tpcb, response, resp_len, TCP_WRITE_FLAG_COPY);

    if (write_err == ERR_OK) {
        tcp_output(tpcb);
        tcp_sent(tpcb, web_sent_callback);
    } else {
        tcp_close(tpcb);
    }
    
    led_blink_activity();
    return ERR_OK;
}

static err_t web_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    
    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }
    
    tcp_recv(newpcb, web_recv_callback);
    return ERR_OK;
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize web server
 */
void web_init(void) {
    if (web_running) {
        printf("[WEB] Already running\n");
        return;
    }

    printf("[WEB] Initializing web interface\n");

    web_pcb = tcp_new();
    if (web_pcb == NULL) {
        printf("[WEB] ERROR: Failed to create TCP PCB\n");
        return;
    }
    
    err_t err = tcp_bind(web_pcb, IP_ADDR_ANY, WEB_PORT);
    if (err != ERR_OK) {
        printf("[WEB] ERROR: Failed to bind to port %d\n", WEB_PORT);
        tcp_close(web_pcb);
        web_pcb = NULL;
        return;
    }
    
    web_pcb = tcp_listen(web_pcb);
    if (web_pcb == NULL) {
        printf("[WEB] ERROR: Failed to listen\n");
        return;
    }
    
    tcp_accept(web_pcb, web_accept_callback);
    
    web_running = true;
    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));
    printf("[WEB] Server running on port %d\n", WEB_PORT);
    printf("[WEB] Status page: http://%s/\n", ip_str);
    printf("[WEB] JSON API: http://%s/api/status\n", ip_str);
}

/**
 * Web server task
 */
void web_task(void) {
    /* Currently nothing needed - all handling is in callbacks */
}

/**
 * Check if web server is running
 */
bool web_is_running(void) {
    return web_running;
}

/**
 * Shutdown web server
 */
void web_shutdown(void) {
    if (web_pcb != NULL) {
        tcp_close(web_pcb);
        web_pcb = NULL;
        web_running = false;
        printf("[WEB] Server stopped\n");
    }
}
