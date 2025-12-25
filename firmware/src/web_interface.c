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
#include "config.h"
#include "ac_freq_monitor.h"
#include "pulse_output.h"
#include "cli.h"

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

#define HTTP_REDIRECT_RESPONSE \
    "HTTP/1.1 303 See Other\r\n" \
    "Location: /config\r\n" \
    "Connection: close\r\n" \
    "\r\n"

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
".nav{text-align:center;margin-bottom:20px}"
".nav a{color:#e94560;text-decoration:none;padding:8px 16px;border:1px solid #e94560;border-radius:8px}"
".nav a:hover{background:#e94560;color:#fff}"
"footer{text-align:center;margin-top:30px;color:#666;font-size:0.9em}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>⚛ CHRONOS-Rb</h1>"
"<div class='nav'><a href='/config'>⚙ Config</a></div>"
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
"<h2>AC Mains Monitor</h2>"
"<div class='stat'><span class='stat-label'>Signal</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Frequency</span>"
"<span class='stat-value'>%.3f Hz</span></div>"
"<div class='stat'><span class='stat-label'>Average</span>"
"<span class='stat-value'>%.3f Hz</span></div>"
"<div class='stat'><span class='stat-label'>Min / Max</span>"
"<span class='stat-value'>%.3f / %.3f Hz</span></div>"
"<div class='stat'><span class='stat-label'>Zero Crossings</span>"
"<span class='stat-value'>%lu</span></div>"
"</div>"
"%s"
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

static const char CONFIG_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<title>CHRONOS-Rb Configuration</title>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 100%%);color:#eee;min-height:100vh;padding:20px}"
".container{max-width:600px;margin:0 auto}"
"h1{text-align:center;margin-bottom:30px;font-size:1.8em;"
"background:linear-gradient(90deg,#e94560,#0f3460);-webkit-background-clip:text;"
"-webkit-text-fill-color:transparent}"
".card{background:rgba(255,255,255,0.05);border-radius:15px;padding:20px;margin-bottom:20px;"
"backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}"
".card h2{color:#e94560;margin-bottom:15px;font-size:1.1em}"
"label{display:block;color:#aaa;margin-bottom:5px;font-size:0.9em}"
"input[type=text],input[type=password]{width:100%%;padding:10px;margin-bottom:15px;"
"background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);border-radius:8px;"
"color:#fff;font-size:1em}"
"input[type=text]:focus,input[type=password]:focus{outline:none;border-color:#e94560}"
".checkbox-row{display:flex;align-items:center;margin-bottom:15px}"
".checkbox-row input{margin-right:10px;width:18px;height:18px}"
".checkbox-row label{margin-bottom:0}"
"button{background:linear-gradient(90deg,#e94560,#0f3460);color:#fff;border:none;"
"padding:12px 30px;border-radius:8px;cursor:pointer;font-size:1em;width:100%%}"
"button:hover{opacity:0.9}"
".nav{text-align:center;margin-bottom:20px}"
".nav a{color:#e94560;text-decoration:none;margin:0 15px}"
".msg{padding:10px;border-radius:8px;margin-bottom:15px;text-align:center}"
".msg-ok{background:rgba(74,222,128,0.2);color:#4ade80}"
".msg-err{background:rgba(248,113,113,0.2);color:#f87171}"
".stat{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.1)}"
".stat:last-child{border-bottom:none}"
".stat-label{color:#aaa;font-size:0.9em}"
".stat-value{font-family:'Courier New',monospace;font-size:0.9em}"
".note{color:#888;font-size:0.85em;margin-top:10px}"
".cli-output{background:#0a0a12;border-radius:8px;padding:12px;font-family:'Courier New',monospace;"
"font-size:0.85em;white-space:pre-wrap;max-height:300px;overflow-y:auto;margin-bottom:15px;color:#0f0}"
".cli-input{display:flex;gap:10px;width:100%%}"
".cli-input input{flex-grow:1;min-width:0;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"border-radius:8px;padding:10px;color:#fff;font-family:'Courier New',monospace}"
".cli-input button{padding:10px 20px;flex-shrink:0}"
"footer{text-align:center;margin-top:30px;color:#666;font-size:0.9em}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>⚙ Configuration</h1>"
"<div class='nav'><a href='/'>← Status</a><a href='/config'>Config</a></div>"
"%s"
"<form method='POST' action='/config'>"
"<div class='card'>"
"<h2>WiFi Settings</h2>"
"<label>SSID</label>"
"<input type='text' name='ssid' value='%s' maxlength='32'>"
"<label>Password</label>"
"<input type='password' name='pass' placeholder='Enter new password' maxlength='64'>"
"<div class='checkbox-row'>"
"<input type='checkbox' name='auto' id='auto' %s>"
"<label for='auto'>Auto-connect on boot</label>"
"</div>"
"</div>"
"<div class='card'>"
"<h2>System Settings</h2>"
"<div class='checkbox-row'>"
"<input type='checkbox' name='debug' id='debug' %s>"
"<label for='debug'>Enable debug output</label>"
"</div>"
"</div>"
"<button type='submit'>Save Configuration</button>"
"</form>"
"%s"
"<div class='card'>"
"<h2>Command Line</h2>"
"<div class='cli-output'>%s</div>"
"<form method='POST' action='/cli' class='cli-input'>"
"<input type='text' name='cmd' placeholder='Enter command (e.g., help, status, pulse list)' autocomplete='off'>"
"<button type='submit'>Run</button>"
"</form>"
"</div>"
"<footer>CHRONOS-Rb v%s</footer>"
"</div>"
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
 * Generate pulse outputs HTML section
 */
static int generate_pulse_outputs_html(char *buf, size_t len) {
    const char *mode_names[] = { "Off", "Interval", "Second", "Minute", "Time" };
    int pos = 0;
    int count = 0;

    /* Count active outputs */
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (p && p->active) count++;
    }

    if (count == 0) {
        return snprintf(buf, len,
            "<div class='card'><h2>Pulse Outputs</h2>"
            "<div class='stat'><span class='stat-label'>Status</span>"
            "<span class='stat-value'>No outputs configured</span></div></div>");
    }

    pos += snprintf(buf + pos, len - pos,
        "<div class='card'><h2>Pulse Outputs</h2>");

    for (int i = 0; i < MAX_PULSE_OUTPUTS && pos < (int)len - 200; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (!p || !p->active) continue;

        char config[64];
        switch (p->mode) {
            case PULSE_MODE_INTERVAL:
                snprintf(config, sizeof(config), "every %lus, %ums",
                    (unsigned long)p->interval, p->pulse_width_ms);
                break;
            case PULSE_MODE_SECOND:
                snprintf(config, sizeof(config), "sec %d, %ums x%d",
                    p->trigger_second, p->pulse_width_ms, p->pulse_count);
                break;
            case PULSE_MODE_MINUTE:
                snprintf(config, sizeof(config), "min %d, %ums x%d",
                    p->trigger_minute, p->pulse_width_ms, p->pulse_count);
                break;
            case PULSE_MODE_TIME:
                snprintf(config, sizeof(config), "%02d:%02d, %ums x%d",
                    p->trigger_hour, p->trigger_minute,
                    p->pulse_width_ms, p->pulse_count);
                break;
            default:
                snprintf(config, sizeof(config), "disabled");
        }

        pos += snprintf(buf + pos, len - pos,
            "<div class='stat'><span class='stat-label'>GP%d (%s)</span>"
            "<span class='stat-value'>%s</span></div>",
            p->gpio_pin, mode_names[p->mode], config);
    }

    pos += snprintf(buf + pos, len - pos, "</div>");
    return pos;
}

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
    
    const ac_freq_state_t *ac = ac_freq_get_state();

    /* Generate pulse outputs section */
    static char pulse_html[512];
    generate_pulse_outputs_html(pulse_html, sizeof(pulse_html));

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
        ac->signal_present ? "Detected" : "Not detected",
        (double)ac->frequency_hz,
        (double)ac->frequency_avg_hz,
        (double)ac->frequency_min_hz,
        (double)ac->frequency_max_hz,
        (unsigned long)ac->zero_cross_count,
        pulse_html,
        ip_str, ip_str,
        CHRONOS_VERSION_STRING
    );
}

/**
 * Generate pulse outputs JSON array
 */
static int generate_pulse_outputs_json(char *buf, size_t len) {
    const char *mode_names[] = { "disabled", "interval", "second", "minute", "time" };
    int pos = 0;

    pos += snprintf(buf + pos, len - pos, "[");

    bool first = true;
    for (int i = 0; i < MAX_PULSE_OUTPUTS && pos < (int)len - 100; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (!p || !p->active) continue;

        if (!first) pos += snprintf(buf + pos, len - pos, ",");
        first = false;

        pos += snprintf(buf + pos, len - pos,
            "{\"pin\":%d,\"mode\":\"%s\",\"interval\":%lu,"
            "\"second\":%d,\"minute\":%d,\"hour\":%d,"
            "\"width_ms\":%d,\"count\":%d,\"gap_ms\":%d}",
            p->gpio_pin, mode_names[p->mode],
            (unsigned long)p->interval,
            p->trigger_second, p->trigger_minute, p->trigger_hour,
            p->pulse_width_ms, p->pulse_count, p->pulse_gap_ms);
    }

    pos += snprintf(buf + pos, len - pos, "]");
    return pos;
}

/**
 * Generate JSON status
 */
static int generate_json_status(char *buf, size_t len) {
    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));
    const ac_freq_state_t *ac = ac_freq_get_state();

    /* Generate pulse outputs JSON */
    static char pulse_json[512];
    generate_pulse_outputs_json(pulse_json, sizeof(pulse_json));

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
        "\"ac_mains\":{"
        "\"signal\":%s,"
        "\"freq_hz\":%.3f,"
        "\"avg_hz\":%.3f,"
        "\"min_hz\":%.3f,"
        "\"max_hz\":%.3f,"
        "\"zero_crossings\":%lu"
        "},"
        "\"pulse_outputs\":%s,"
        "\"ip\":\"%s\","
        "\"version\":\"%s\""
        "}",
        (int)g_time_state.sync_state,
        g_time_state.rb_locked ? "true" : "false",
        g_time_state.time_valid ? "true" : "false",
        (long long)g_time_state.offset_ns,
        (double)g_time_state.frequency_offset,
        (unsigned long)g_time_state.pps_count,
        (unsigned long)g_time_state.last_freq_count,
        (unsigned long)g_stats.ntp_requests,
        (unsigned long)g_stats.ptp_sync_sent,
        ac->signal_present ? "true" : "false",
        (double)ac->frequency_hz,
        (double)ac->frequency_avg_hz,
        (double)ac->frequency_min_hz,
        (double)ac->frequency_max_hz,
        (unsigned long)ac->zero_cross_count,
        pulse_json,
        ip_str,
        CHRONOS_VERSION_STRING
    );
}

/**
 * Generate JSON config
 */
static int generate_json_config(char *buf, size_t len) {
    config_t *cfg = config_get();

    return snprintf(buf, len,
        "{"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_enabled\":%s,"
        "\"debug_enabled\":%s"
        "}",
        cfg->wifi_ssid,
        cfg->wifi_enabled ? "true" : "false",
        g_debug_enabled ? "true" : "false"
    );
}

/**
 * Generate pulse outputs section for config page
 */
static int generate_pulse_config_html(char *buf, size_t len) {
    const char *mode_names[] = { "Off", "Interval", "Second", "Minute", "Time" };
    int pos = 0;
    int count = 0;

    /* Count active outputs */
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (p && p->active) count++;
    }

    pos += snprintf(buf + pos, len - pos,
        "<div class='card'><h2>Pulse Outputs</h2>");

    if (count == 0) {
        pos += snprintf(buf + pos, len - pos,
            "<div class='stat'><span class='stat-label'>Status</span>"
            "<span class='stat-value'>No outputs configured</span></div>");
    } else {
        for (int i = 0; i < MAX_PULSE_OUTPUTS && pos < (int)len - 200; i++) {
            pulse_config_t *p = pulse_output_get(i);
            if (!p || !p->active) continue;

            char config[64];
            switch (p->mode) {
                case PULSE_MODE_INTERVAL:
                    snprintf(config, sizeof(config), "every %lus, %ums",
                        (unsigned long)p->interval, p->pulse_width_ms);
                    break;
                case PULSE_MODE_SECOND:
                    snprintf(config, sizeof(config), "sec %d, %ums x%d",
                        p->trigger_second, p->pulse_width_ms, p->pulse_count);
                    break;
                case PULSE_MODE_MINUTE:
                    snprintf(config, sizeof(config), "min %d, %ums x%d",
                        p->trigger_minute, p->pulse_width_ms, p->pulse_count);
                    break;
                case PULSE_MODE_TIME:
                    snprintf(config, sizeof(config), "%02d:%02d, %ums x%d",
                        p->trigger_hour, p->trigger_minute,
                        p->pulse_width_ms, p->pulse_count);
                    break;
                default:
                    snprintf(config, sizeof(config), "disabled");
            }

            pos += snprintf(buf + pos, len - pos,
                "<div class='stat'><span class='stat-label'>GP%d (%s)</span>"
                "<span class='stat-value'>%s</span></div>",
                p->gpio_pin, mode_names[p->mode], config);
        }
    }

    pos += snprintf(buf + pos, len - pos,
        "<p class='note'>Configure via CLI: pulse &lt;pin&gt; &lt;mode&gt; ...</p></div>");

    return pos;
}

/**
 * Generate config page HTML
 */
static int generate_config_page(char *buf, size_t len, const char *message, const char *cli_output) {
    config_t *cfg = config_get();

    /* Generate pulse outputs section */
    static char pulse_html[512];
    generate_pulse_config_html(pulse_html, sizeof(pulse_html));

    return snprintf(buf, len, CONFIG_PAGE,
        message ? message : "",
        cfg->wifi_ssid,
        cfg->wifi_enabled ? "checked" : "",
        g_debug_enabled ? "checked" : "",
        pulse_html,
        cli_output ? cli_output : "Type a command and press Run",
        CHRONOS_VERSION_STRING
    );
}

/**
 * URL decode a string in place
 */
static void url_decode(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * Parse form field from POST body
 */
static bool parse_form_field(const char *body, const char *field, char *value, size_t max_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);

    const char *start = strstr(body, search);
    if (!start) {
        value[0] = '\0';
        return false;
    }

    start += strlen(search);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= max_len) len = max_len - 1;

    strncpy(value, start, len);
    value[len] = '\0';
    url_decode(value);
    return true;
}

/**
 * Check if form field (checkbox) is present
 */
static bool parse_form_checkbox(const char *body, const char *field) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);
    return strstr(body, search) != NULL;
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

    /* Copy request to buffer for parsing */
    static char request[2048];
    size_t copy_len = p->tot_len < sizeof(request) - 1 ? p->tot_len : sizeof(request) - 1;
    memcpy(request, p->payload, copy_len);
    request[copy_len] = '\0';

    pbuf_free(p);

    /* Allocate response buffer */
    static char response[8192];
    static char html_buf[6000];
    static char cli_output[1024];
    const char *msg = NULL;
    const char *cli_out = NULL;

    /* Parse HTTP method and path */
    bool is_post = (strncmp(request, "POST ", 5) == 0);

    if (strstr(request, "/api/status") != NULL) {
        /* JSON status API */
        char json_buf[512];
        generate_json_status(json_buf, sizeof(json_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_JSON_HEADER, json_buf);

    } else if (strstr(request, "/api/config") != NULL) {
        /* JSON config API */
        char json_buf[256];
        generate_json_config(json_buf, sizeof(json_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_JSON_HEADER, json_buf);

    } else if (is_post && strstr(request, "/cli") != NULL) {
        /* POST /cli - execute CLI command */
        const char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            char cmd[128];
            parse_form_field(body, "cmd", cmd, sizeof(cmd));
            if (cmd[0]) {
                cli_execute(cmd, cli_output, sizeof(cli_output));
                cli_out = cli_output;
            }
        }
        generate_config_page(html_buf, sizeof(html_buf), NULL, cli_out);
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (is_post && strstr(request, "/config") != NULL) {
        /* POST /config - save configuration */
        const char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;

            char ssid[33], pass[65];
            parse_form_field(body, "ssid", ssid, sizeof(ssid));
            parse_form_field(body, "pass", pass, sizeof(pass));
            bool auto_connect = parse_form_checkbox(body, "auto");
            bool debug = parse_form_checkbox(body, "debug");

            /* Update debug flag */
            g_debug_enabled = debug;

            /* Update WiFi config if SSID provided */
            if (ssid[0]) {
                config_set_wifi(ssid, pass[0] ? pass : NULL, auto_connect);
            }

            /* Save to flash */
            if (config_save()) {
                msg = "<div class='msg msg-ok'>Configuration saved!</div>";
            } else {
                msg = "<div class='msg msg-err'>Failed to save configuration</div>";
            }
        }
        generate_config_page(html_buf, sizeof(html_buf), msg, NULL);
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (strstr(request, "GET /config") != NULL) {
        /* GET /config - show config page */
        generate_config_page(html_buf, sizeof(html_buf), NULL, NULL);
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (strstr(request, "GET / ") != NULL || strstr(request, "GET /index") != NULL) {
        /* Status page */
        generate_status_page(html_buf, sizeof(html_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else {
        strcpy(response, HTTP_404_RESPONSE);
    }

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
