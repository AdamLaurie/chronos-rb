/**
 * CHRONOS-Rb GPS Input Module
 *
 * Handles GPS receiver input (NEO-M8N or similar) for:
 * - Backup time source when rubidium is unavailable
 * - Initial time setting before rubidium locks
 * - Position information for PTP/NTP
 *
 * Connections:
 *   GPS TX  -> GP5 (UART1 RX)
 *   GPS RX  <- GP4 (UART1 TX, for UBX commands)
 *   GPS PPS -> GP11
 *   GPS VCC -> 3.3V
 *   GPS GND -> GND
 *
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#ifndef GPS_INPUT_H
#define GPS_INPUT_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * GPS STATUS STRUCTURES
 *============================================================================*/

/* GPS fix type */
typedef enum {
    GPS_FIX_NONE = 0,       /* No fix */
    GPS_FIX_2D = 2,         /* 2D fix (no altitude) */
    GPS_FIX_3D = 3          /* 3D fix */
} gps_fix_type_t;

/* GPS time data */
typedef struct {
    uint16_t year;          /* UTC year (e.g., 2025) */
    uint8_t month;          /* UTC month (1-12) */
    uint8_t day;            /* UTC day (1-31) */
    uint8_t hour;           /* UTC hour (0-23) */
    uint8_t minute;         /* UTC minute (0-59) */
    uint8_t second;         /* UTC second (0-59) */
    uint16_t millisecond;   /* Milliseconds (0-999) */
    bool valid;             /* Time data valid */
} gps_time_t;

/* GPS position data */
typedef struct {
    double latitude;        /* Latitude in degrees (+ = N, - = S) */
    double longitude;       /* Longitude in degrees (+ = E, - = W) */
    double altitude;        /* Altitude in meters (MSL) */
    double speed_knots;     /* Speed over ground in knots */
    double course;          /* Course over ground in degrees */
    double hdop;            /* Horizontal dilution of precision */
    bool valid;             /* Position data valid */
} gps_position_t;

/* Complete GPS state */
typedef struct {
    gps_time_t time;        /* UTC time from GPS */
    gps_position_t position; /* Position from GPS */
    gps_fix_type_t fix_type; /* Current fix type */
    uint8_t satellites;     /* Number of satellites in use */
    uint8_t satellites_view; /* Number of satellites in view */
    bool pps_valid;         /* PPS signal present and valid */
    uint64_t last_pps_us;   /* Timestamp of last PPS (us since boot) */
    uint64_t last_nmea_us;  /* Timestamp of last valid NMEA */
    uint32_t pps_count;     /* Total PPS pulses received */
    uint32_t nmea_count;    /* Total NMEA sentences parsed */
    uint32_t nmea_errors;   /* NMEA parse errors */
} gps_state_t;

/*============================================================================
 * FUNCTION PROTOTYPES
 *============================================================================*/

/* Initialization */
void gps_input_init(void);

/* Task - call from main loop */
void gps_input_task(void);

/* Status queries */
bool gps_has_fix(void);
bool gps_has_time(void);
bool gps_pps_valid(void);
uint8_t gps_get_satellites(void);
gps_fix_type_t gps_get_fix_type(void);

/* Time access */
uint32_t gps_get_unix_time(void);
void gps_get_utc_time(gps_time_t *time);
uint64_t gps_get_last_nmea_us(void);

/* PPS access */
uint64_t gps_get_last_pps_us(void);
uint32_t gps_get_pps_count(void);

/* Position access */
void gps_get_position(double *lat, double *lon, double *alt);
void gps_get_position_full(gps_position_t *pos);

/* Full state access */
const gps_state_t* gps_get_state(void);

/* Control */
void gps_enable(bool enable);
bool gps_is_enabled(void);
void gps_reset_time(void);
void gps_set_debug(bool enable);
bool gps_get_debug(void);

/* Module info */
const char* gps_get_firmware_version(void);
const char* gps_get_hardware_version(void);
int8_t gps_get_leap_seconds(void);
bool gps_leap_seconds_is_valid(void);

#endif /* GPS_INPUT_H */
