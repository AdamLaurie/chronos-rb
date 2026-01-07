/**
 * CHRONOS-Rb: Compact High-precision Rubidium Oscillator Network Operating System
 * 
 * Main configuration header for Raspberry Pi Pico 2-W NTP/PTP Server
 * synchronized to FE-5680A Rubidium Frequency Standard
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef CHRONOS_RB_H
#define CHRONOS_RB_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * VERSION INFORMATION
 *============================================================================*/
#define CHRONOS_VERSION_MAJOR   1
#define CHRONOS_VERSION_MINOR   4
#define CHRONOS_VERSION_PATCH   24
#define CHRONOS_VERSION_STRING  "1.4.24"
#define CHRONOS_BUILD_DATE      __DATE__
#define CHRONOS_BUILD_TIME      __TIME__

/*============================================================================
 * GPIO PIN DEFINITIONS - Raspberry Pi Pico 2-W
 *============================================================================*/

/* FE-5680A 1PPS Input (active high, from external FE-5680A source) */
#define GPIO_FE_PPS_INPUT       21      /* GP21 - 1PPS from FE-5680A */
#define GPIO_PPS_INPUT          GPIO_FE_PPS_INPUT  /* Primary PPS source */

/* FE-5680A 10MHz Reference Input (after comparator, 3.3V LVCMOS) */
#define GPIO_FE_10MHZ_INPUT     20      /* GP20 - FE-5680A 10MHz reference */
#define GPIO_10MHZ_INPUT        GPIO_FE_10MHZ_INPUT  /* Primary 10MHz source */

/* FE-5680A Status/Control Lines */
#define GPIO_RB_LOCK_STATUS     22      /* GP22 - Rubidium lock indicator (HIGH=locked) */

/* Status LEDs */
#define GPIO_LED_SYNC           6       /* GP6 - Green: Synchronized to Rb */
#define GPIO_LED_NETWORK        7       /* GP7 - Blue: Network connected */
#define GPIO_LED_ACTIVITY       8       /* GP8 - Yellow: NTP/PTP activity */
#define GPIO_LED_ERROR          9       /* GP9 - Red: Error condition */

/* Debug/Diagnostic Outputs */
#define GPIO_DEBUG_PPS_OUT      10      /* GP10 - Regenerated 1PPS for test */

/* GPS Receiver Input (NEO-M8N or similar) */
#define GPIO_GPS_PPS_INPUT      11      /* GP11 - GPS 1PPS input (backup time source) */

/* UART for Debug (optional) */
#define GPIO_UART_TX            0       /* GP0 - UART0 TX */
#define GPIO_UART_RX            1       /* GP1 - UART0 RX */

/* I2C for optional OLED display */
#define GPIO_I2C_SDA            12      /* GP12 - I2C0 SDA */
#define GPIO_I2C_SCL            13      /* GP13 - I2C0 SCL */

/* Interval Pulse Outputs (active high, ~10ms pulse width) */
#define GPIO_PULSE_500MS        14      /* GP14 - 0.5 second interval pulse */
#define GPIO_PULSE_1S           15      /* GP15 - 1 second interval pulse */
#define GPIO_PULSE_6S           16      /* GP16 - 6 second interval pulse */
#define GPIO_PULSE_30S          17      /* GP17 - 30 second interval pulse */
#define GPIO_PULSE_60S          18      /* GP18 - 60 second interval pulse */

/* AC Mains Frequency Monitor Input */
#define GPIO_AC_ZERO_CROSS      19      /* GP19 - Zero-crossing detector input */

/* Radio Timecode Outputs (simulated LF radio signals) */
#define GPIO_DCF77              2       /* GP2 - DCF77 Germany 77.5kHz */
#define GPIO_WWVB               3       /* GP3 - WWVB USA 60kHz */
#define GPIO_JJY40              4       /* GP4 - JJY Japan 40kHz (Fukushima) */
#define GPIO_JJY60              26      /* GP26 - JJY Japan 60kHz (Kyushu) */

/* IRIG-B Timecode Output */
#define GPIO_IRIG_B             27      /* GP27 - IRIG-B timecode */

/* NMEA/GPS Serial (UART1) */
#define GPIO_GPS_TX             4       /* GP4 - UART1 TX (commands to GPS module) */
#define GPIO_GPS_RX             5       /* GP5 - UART1 RX (NMEA from GPS module) */

/* Interval pulse timing */
#define PULSE_WIDTH_MS          10      /* Output pulse width in milliseconds */

/*============================================================================
 * TIMING CONSTANTS
 *============================================================================*/

/* System clock configuration */
#define SYSTEM_CLOCK_HZ         150000000UL     /* 150 MHz system clock */
#define REF_CLOCK_HZ            10000000UL      /* 10 MHz rubidium reference */

/* PPS timing parameters */
#define PPS_NOMINAL_PERIOD_US   1000000UL       /* 1 second in microseconds */
#define PPS_TOLERANCE_US        100             /* ±100µs tolerance for lock */
#define PPS_PULSE_MIN_US        10              /* Minimum valid pulse width */
#define PPS_PULSE_MAX_US        500             /* Maximum valid pulse width */

/* Frequency counter parameters */
#define FREQ_GATE_TIME_MS       1000            /* 1 second gate time */
#define FREQ_NOMINAL_COUNT      10000000UL      /* Expected count in gate time */
#define FREQ_TOLERANCE_PPB      1000            /* ±1000 ppb tolerance */

/* Time discipline parameters */
#define DISCIPLINE_TAU_FAST     64              /* Fast time constant (seconds) */
#define DISCIPLINE_TAU_SLOW     1024            /* Slow time constant (seconds) */
#define DISCIPLINE_GAIN_P       0.7             /* Proportional gain */
#define DISCIPLINE_GAIN_I       0.3             /* Integral gain */

/* GPS receiver parameters */
#define GPS_UART_BAUD           9600            /* NEO-M8N default baud rate */
#define GPS_PPS_TIMEOUT_MS      2000            /* GPS PPS timeout (2 seconds) */
#define GPS_NMEA_TIMEOUT_MS     5000            /* NMEA sentence timeout */
#define GPS_MIN_SATS            4               /* Minimum satellites for valid fix */

/*============================================================================
 * NETWORK CONFIGURATION
 *============================================================================*/

/* WiFi defaults (can be overridden via web interface) */
#define WIFI_SSID_DEFAULT       "CHRONOS-Rb"
#define WIFI_PASS_DEFAULT       "rubidium123"
#define WIFI_COUNTRY            "US"

/* NTP Server Configuration */
#define NTP_PORT                123
#define NTP_VERSION             4
#define NTP_STRATUM             1               /* Stratum 1 - Primary reference */
#define NTP_POLL_MIN            4               /* 16 seconds minimum */
#define NTP_POLL_MAX            10              /* 1024 seconds maximum */
#define NTP_PRECISION           -20             /* ~1 microsecond precision */
#define NTP_REFID               "RBDM"          /* Rubidium reference ID */

/* PTP (IEEE 1588) Configuration */
#define PTP_EVENT_PORT          319
#define PTP_GENERAL_PORT        320
#define PTP_DOMAIN              0
#define PTP_PRIORITY1           128
#define PTP_PRIORITY2           128
#define PTP_CLOCK_CLASS         6               /* Primary reference source */
#define PTP_CLOCK_ACCURACY      0x21            /* 100ns accuracy class */

/* Web Interface */
#define WEB_PORT                80
#define WEB_MAX_CONNECTIONS     4

/*============================================================================
 * BUFFER SIZES AND LIMITS
 *============================================================================*/

#define NTP_PACKET_SIZE         48
#define PTP_SYNC_SIZE           44
#define PTP_FOLLOWUP_SIZE       44
#define PTP_DELAY_REQ_SIZE      44
#define PTP_DELAY_RESP_SIZE     54

#define MAX_NTP_CLIENTS         32
#define MAX_PTP_CLIENTS         16

#define TIMESTAMP_BUFFER_SIZE   64
#define LOG_BUFFER_SIZE         4096

/*============================================================================
 * DATA STRUCTURES
 *============================================================================*/

/* High-resolution timestamp (similar to NTP timestamp) */
typedef struct {
    uint32_t seconds;           /* Seconds since epoch */
    uint32_t fraction;          /* Fractional seconds (2^32 = 1 second) */
} timestamp_t;

/* System time state */
typedef struct {
    timestamp_t current_time;   /* Current time */
    int64_t offset_ns;          /* Offset from rubidium reference (ns) */
    double frequency_offset;    /* Frequency offset (ppb) */
    double drift_rate;          /* Drift rate (ppb/s) */
    uint32_t pps_count;         /* Number of PPS pulses received */
    uint32_t last_freq_count;   /* Last frequency measurement */
    bool rb_locked;             /* Rubidium oscillator locked */
    bool time_valid;            /* Time is valid and synchronized */
    uint8_t sync_state;         /* Synchronization state machine */
} time_state_t;

/* Synchronization states */
typedef enum {
    SYNC_STATE_INIT = 0,        /* Initial state, waiting for lock */
    SYNC_STATE_FREQ_CAL,        /* Frequency calibration */
    SYNC_STATE_COARSE,          /* Coarse time acquisition */
    SYNC_STATE_FINE,            /* Fine time discipline */
    SYNC_STATE_LOCKED,          /* Fully synchronized */
    SYNC_STATE_HOLDOVER,        /* Lost reference, using holdover */
    SYNC_STATE_ERROR            /* Error condition */
} sync_state_t;

/* NTP packet structure */
typedef struct __attribute__((packed)) {
    uint8_t li_vn_mode;         /* Leap indicator, version, mode */
    uint8_t stratum;            /* Stratum level */
    int8_t poll;                /* Poll interval */
    int8_t precision;           /* Precision */
    uint32_t root_delay;        /* Root delay */
    uint32_t root_dispersion;   /* Root dispersion */
    uint32_t ref_id;            /* Reference ID */
    uint32_t ref_ts_sec;        /* Reference timestamp seconds */
    uint32_t ref_ts_frac;       /* Reference timestamp fraction */
    uint32_t orig_ts_sec;       /* Origin timestamp seconds */
    uint32_t orig_ts_frac;      /* Origin timestamp fraction */
    uint32_t rx_ts_sec;         /* Receive timestamp seconds */
    uint32_t rx_ts_frac;        /* Receive timestamp fraction */
    uint32_t tx_ts_sec;         /* Transmit timestamp seconds */
    uint32_t tx_ts_frac;        /* Transmit timestamp fraction */
} ntp_packet_t;

/* Statistics */
typedef struct {
    uint32_t ntp_requests;      /* Total NTP requests served */
    uint32_t ptp_sync_sent;     /* PTP Sync messages sent */
    uint32_t ptp_delay_resp;    /* PTP Delay responses sent */
    uint32_t pps_interrupts;    /* PPS interrupt count */
    uint32_t freq_measurements; /* Frequency measurements */
    uint32_t errors;            /* Error count */
    int32_t min_offset_ns;      /* Minimum offset seen */
    int32_t max_offset_ns;      /* Maximum offset seen */
    double avg_offset_ns;       /* Average offset */
} statistics_t;

/*============================================================================
 * GLOBAL VARIABLES (extern declarations)
 *============================================================================*/

extern volatile time_state_t g_time_state;
extern volatile statistics_t g_stats;
extern volatile bool g_wifi_connected;
extern volatile bool g_debug_enabled;

/*============================================================================
 * FUNCTION PROTOTYPES
 *============================================================================*/

/* Initialization */
void chronos_init(void);
void gpio_init_all(void);
void led_init(void);

/* Time management */
void time_init(void);
timestamp_t get_current_time(void);
void set_time(timestamp_t *ts);
uint64_t get_time_us(void);

/* Rubidium synchronization */
void rubidium_sync_init(void);
void rubidium_sync_task(void);
bool rubidium_is_locked(void);
void force_time_resync(void);

/* PPS capture */
void pps_capture_init(void);
void pps_irq_handler(void);
uint64_t get_last_pps_timestamp(void);
bool is_pps_valid(void);

/* Frequency counter - hardware PPS validation */
void freq_counter_init(void);
void freq_counter_pps_start(void);           /* Legacy: no-op, PIO handles automatically */
uint32_t freq_counter_read_legacy(void);     /* Get last count */
uint32_t freq_counter_read_count(void);      /* Get last count (preferred) */
int32_t freq_counter_get_error(void);        /* Get deviation from 10,000,000 */
bool freq_counter_new_measurement(void);     /* Check if new measurement available */
double get_frequency_offset_ppb(void);
bool freq_counter_signal_present(void);

/* PPS offset measurement (FE PPS vs GPS PPS, 10MHz locked) */
void freq_counter_pps_task(void);            /* Poll PIO FIFOs - call from main loop */
void freq_counter_capture_gps_pps(void);     /* Legacy no-op */
int32_t freq_counter_get_pps_offset(void);   /* Get offset in 10MHz ticks */
double freq_counter_get_pps_drift(void);     /* Get drift rate (ticks/sec) */
double freq_counter_get_pps_stddev(void);    /* Get offset std deviation */
bool freq_counter_pps_offset_valid(void);    /* Check if offset is valid */
uint32_t freq_counter_get_fe_pps_count(void);  /* Debug: FE PPS capture count */
uint32_t freq_counter_get_gps_pps_count(void); /* Debug: GPS PPS capture count */
bool freq_counter_fe_pps_valid(void);        /* Check if FE PPS capture valid */
bool freq_counter_gps_pps_valid(void);       /* Check if GPS PPS capture valid */

/* Time discipline */
void discipline_init(void);
void discipline_update(int64_t offset_ns);
double discipline_get_correction(void);
bool discipline_is_locked(void);
void discipline_reset(void);

/* NTP server */
void ntp_server_init(void);
void ntp_server_task(void);

/* PTP server */
void ptp_server_init(void);
void ptp_server_task(void);
void ptp_send_sync(void);
void ptp_send_announce(void);

/* WiFi management */
void wifi_init(void);
bool wifi_connect(const char *ssid, const char *password);
bool wifi_is_connected(void);
void wifi_task(void);
void get_ip_address_str(char *buf, size_t len);

/* Web interface */
void web_init(void);
void web_task(void);

/* LED control */
void led_set_sync(bool on);
void led_set_network(bool on);
void led_set_activity(bool on);
void led_set_error(bool on);
void led_blink_activity(void);

/* Debug/Logging */
void debug_init(void);
void debug_print(const char *fmt, ...);
void debug_print_stats(void);

/* AC Mains Frequency Monitor */
void ac_freq_init(void);
void ac_freq_task(void);
float ac_freq_get_hz(void);
bool ac_freq_is_valid(void);

/* GPS Receiver Input */
void gps_input_init(void);
void gps_input_task(void);
bool gps_has_fix(void);
bool gps_has_time(void);
bool gps_pps_valid(void);
uint8_t gps_get_satellites(void);
uint32_t gps_get_unix_time(void);
uint64_t gps_get_last_pps_us(void);
void gps_get_position(double *lat, double *lon, double *alt);

#endif /* CHRONOS_RB_H */
