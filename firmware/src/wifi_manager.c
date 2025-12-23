/**
 * CHRONOS-Rb WiFi Manager Module
 * 
 * Manages WiFi connectivity for the Pico 2-W.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"

#include "chronos_rb.h"

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static bool wifi_initialized = false;
static bool wifi_connected = false;
static char current_ssid[33] = {0};
static uint32_t ip_address = 0;

/* Connection state tracking */
static uint32_t connection_attempts = 0;
static uint32_t last_connection_time = 0;
static uint32_t disconnection_count = 0;

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize WiFi subsystem
 */
void wifi_init(void) {
    printf("[WIFI] Initializing CYW43 WiFi...\n");
    
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        printf("[WIFI] ERROR: Failed to initialize WiFi\n");
        return;
    }
    
    cyw43_arch_enable_sta_mode();
    
    wifi_initialized = true;
    printf("[WIFI] WiFi initialized successfully\n");
    
    /* Print MAC address */
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    printf("[WIFI] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/*============================================================================
 * CONNECTION MANAGEMENT
 *============================================================================*/

/**
 * Connect to WiFi network
 */
bool wifi_connect(const char *ssid, const char *password) {
    if (!wifi_initialized) {
        printf("[WIFI] ERROR: WiFi not initialized\n");
        return false;
    }

    printf("[WIFI] Connecting to '%s'...\n", ssid);
    connection_attempts++;

    /* Attempt connection with timeout */
    int result = cyw43_arch_wifi_connect_timeout_ms(
        ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000);
    
    if (result != 0) {
        printf("[WIFI] ERROR: Connection failed (error %d)\n", result);
        wifi_connected = false;
        g_wifi_connected = false;
        return false;
    }
    
    /* Get IP address from default netif */
    struct netif *netif = netif_default;
    if (netif != NULL) {
        ip_address = ip4_addr_get_u32(netif_ip4_addr(netif));
        printf("[WIFI] Connected! IP: %lu.%lu.%lu.%lu\n",
               ip_address & 0xFF,
               (ip_address >> 8) & 0xFF,
               (ip_address >> 16) & 0xFF,
               (ip_address >> 24) & 0xFF);
    }
    
    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    wifi_connected = true;
    g_wifi_connected = true;
    last_connection_time = time_us_32() / 1000000;
    
    return true;
}

/**
 * Disconnect from WiFi
 */
void wifi_disconnect(void) {
    if (wifi_connected) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        wifi_connected = false;
        g_wifi_connected = false;
        ip_address = 0;
        printf("[WIFI] Disconnected\n");
    }
}

/**
 * Check if WiFi is connected
 */
bool wifi_is_connected(void) {
    return wifi_connected && (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN);
}

/**
 * Get current IP address
 */
uint32_t get_ip_address(void) {
    return ip_address;
}

/**
 * Get IP address as string
 */
void get_ip_address_str(char *buf, size_t len) {
    snprintf(buf, len, "%lu.%lu.%lu.%lu",
             ip_address & 0xFF,
             (ip_address >> 8) & 0xFF,
             (ip_address >> 16) & 0xFF,
             (ip_address >> 24) & 0xFF);
}

/*============================================================================
 * WIFI TASK
 *============================================================================*/

/**
 * WiFi maintenance task - call periodically
 */
void wifi_task(void) {
    static uint64_t last_check = 0;
    uint64_t now = time_us_64();
    
    /* Check connection status every second */
    if (now - last_check < 1000000) {
        return;
    }
    last_check = now;
    
    if (!wifi_initialized) {
        return;
    }
    
    /* Check link status */
    int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    
    if (wifi_connected && link_status != CYW43_LINK_JOIN) {
        /* Lost connection */
        printf("[WIFI] Connection lost (status %d)\n", link_status);
        wifi_connected = false;
        g_wifi_connected = false;
        ip_address = 0;
        disconnection_count++;
        
        /* Attempt reconnection */
        printf("[WIFI] Attempting reconnection...\n");
        wifi_connect(current_ssid, "");  /* Will need stored password */
    }
    
    /* Poll lwIP */
    cyw43_arch_poll();
}

/**
 * Get WiFi signal strength (RSSI)
 */
int32_t wifi_get_rssi(void) {
    int32_t rssi;
    if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) {
        return rssi;
    }
    return 0;
}

/**
 * Get WiFi statistics
 */
void wifi_get_statistics(uint32_t *attempts, uint32_t *disconnects) {
    *attempts = connection_attempts;
    *disconnects = disconnection_count;
}

/**
 * Scan for available networks
 */
void wifi_scan(void) {
    printf("[WIFI] Scanning for networks...\n");
    
    /* Note: Full scan implementation would require callback handling */
    /* This is a simplified version */
    
    cyw43_wifi_scan_options_t scan_options = {0};
    int result = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, NULL);
    
    if (result != 0) {
        printf("[WIFI] Scan failed: %d\n", result);
    }
}

/**
 * Get current SSID
 */
const char* wifi_get_ssid(void) {
    return current_ssid;
}

/**
 * Check if WiFi is initialized
 */
bool wifi_is_initialized(void) {
    return wifi_initialized;
}
