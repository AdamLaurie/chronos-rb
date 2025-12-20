/**
 * CHRONOS-Rb lwIP Options
 *
 * Configuration for the lightweight IP stack on Pico W
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/*============================================================================
 * PLATFORM CONFIGURATION
 *============================================================================*/

/* Use the threadsafe background mode for CYW43 */
#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

/* Single-threaded mode settings */
#define SYS_LIGHTWEIGHT_PROT        0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4

/*============================================================================
 * MEMORY CONFIGURATION
 *============================================================================*/

#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_PCB            5
#define MEMP_NUM_TCP_PCB_LISTEN     8
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_UDP_PCB            8
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_NETBUF             2
#define MEMP_NUM_NETCONN            4
#define MEMP_NUM_PBUF               24
#define MEMP_NUM_SYS_TIMEOUT        8

/* Pbuf pool size */
#define PBUF_POOL_SIZE              24

/*============================================================================
 * CORE lWIP MODULES
 *============================================================================*/

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

/*============================================================================
 * UDP CONFIGURATION (for NTP/PTP)
 *============================================================================*/

#define LWIP_UDP                    1
#define UDP_TTL                     64

/*============================================================================
 * TCP CONFIGURATION (for Web Interface)
 *============================================================================*/

#define LWIP_TCP                    1
#define TCP_TTL                     64
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1))/(TCP_MSS))
#define TCP_QUEUE_OOSEQ             0
#define TCP_OVERSIZE                TCP_MSS

/*============================================================================
 * DHCP CONFIGURATION
 *============================================================================*/

#define LWIP_DHCP                   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_AUTOIP                 0

/*============================================================================
 * DNS CONFIGURATION
 *============================================================================*/

#define LWIP_DNS                    1
#define DNS_MAX_SERVERS             2

/*============================================================================
 * NETWORK INTERFACE
 *============================================================================*/

#define LWIP_SINGLE_NETIF           1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1

/*============================================================================
 * CALLBACK STYLE API
 *============================================================================*/

#define LWIP_CALLBACK_API           1

/*============================================================================
 * DEBUG OPTIONS (disabled for production)
 *============================================================================*/

#define LWIP_DEBUG                  0

/*============================================================================
 * STATISTICS (disabled to save memory)
 *============================================================================*/

#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

/*============================================================================
 * CHECKSUM CONFIGURATION
 *============================================================================*/

/* Use software checksums */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1

/*============================================================================
 * SNTP SUPPORT
 *============================================================================*/

#define SNTP_SERVER_DNS             1

#endif /* LWIPOPTS_H */
