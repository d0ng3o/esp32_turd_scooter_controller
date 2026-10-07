// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// config.h - persistent, web-UI-editable settings (NVS-backed).
//
// One versioned struct holds every tunable. config_init() loads it from NVS (or
// writes defaults on first boot / version bump). The WiFi config UI edits g_cfg
// and calls config_save(). Ride code reads g_cfg live.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CONFIG_MAGIC    0x54555244u   // 'TURD'
#define CONFIG_VERSION  1

typedef enum {
    HEADLIGHT_AUTO = 0,   // on while moving (original behaviour)
    HEADLIGHT_ON,         // always on
    HEADLIGHT_OFF,        // always off
    HEADLIGHT_MANUAL,     // rider toggles via gesture
} headlight_mode_t;

typedef enum {
    BRAKELIGHT_STEADY = 0,      // tail stays on (no brake effect)
    BRAKELIGHT_STROBE_TAIL,     // experimental: strobe the tail flag on brake
    BRAKELIGHT_WS2812,          // flash the onboard status LED red on brake
} brakelight_mode_t;

typedef enum { UNITS_KMH = 0, UNITS_MPH } units_t;

typedef enum { THROTTLE_LINEAR = 0, THROTTLE_PROGRESSIVE } throttle_curve_t;

typedef struct {
    uint32_t magic;
    uint16_t version;

    // --- throttle / brake calibration (raw 12-bit ADC endpoints) ---
    uint16_t thr_raw_min, thr_raw_max;
    uint16_t brk_raw_min, brk_raw_max;
    uint8_t  thr_deadzone_pct;      // ignore bottom N % of throttle travel
    uint8_t  brk_active_pct;        // brake "on" past N % of travel

    // --- ride feel ---
    uint8_t  throttle_cap;          // max byte sent to controller (0..255)
    uint16_t soft_start_ms;         // ramp time 0 -> cap (0 = instant)
    uint8_t  throttle_curve;        // throttle_curve_t
    bool     kick_to_start;         // require wheel motion before throttle

    // --- lock / security ---
    uint16_t auto_lock_timeout_s;   // inactivity before auto-lock (0 = never)
    bool     motion_alarm;          // buzzer/LED if moved while locked

    // --- lights ---
    uint8_t  headlight_mode;        // headlight_mode_t
    uint8_t  brakelight_mode;       // brakelight_mode_t
    uint16_t headlight_off_delay_ms;// keep headlight on this long after stopping

    // --- IMU (wake-on-motion / alarm) ---
    uint8_t  imu_wake_sens;         // motion threshold (lower = more sensitive)
    uint16_t auto_sleep_timeout_s;  // idle before deep sleep (0 = never)

    // --- feedback ---
    bool     buzzer_enable;
    uint8_t  led_brightness;        // 0..255 scale on the status LED

    // --- WiFi / OTA ---
    char     ap_ssid[32];
    char     ap_pass[64];           // >= 8 chars for WPA2
    uint16_t config_timeout_s;      // auto-exit config mode after inactivity

    // --- system ---
    uint8_t  units;                 // units_t
} config_t;

extern config_t g_cfg;              // the live configuration

// Load from NVS (or write defaults on first boot / version mismatch). Also
// initialises the NVS flash partition.
void config_init(void);

// Persist g_cfg to NVS.
esp_err_t config_save(void);

// Populate *c with compiled-in defaults (see board.h).
void config_set_defaults(config_t *c);
