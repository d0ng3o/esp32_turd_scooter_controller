// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// control.h - ride state machine, safety interlocks, ride-feel shaping, and
// light flag computation.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "inputs.h"
#include "protocol.h"

typedef enum {
    RIDE_FAULT = 0,   // no bus link: throttle forced 0
    RIDE_LOCKED,      // scooter locked: throttle inhibited until unlocked
    RIDE_IDLE,        // link ok, wheel stopped: waiting for a kick
    RIDE_READY,       // kicked + throttle released: armed to accept throttle
    RIDE_RIDING,      // delivering throttle to the controller
    RIDE_BRAKING,     // brake engaged: throttle forced 0
} ride_state_t;

typedef struct {
    uint8_t      throttle;    // 0..255 byte to send in the poll
    bool         headlight;   // headlight flag (0x40)
    bool         tail_on;     // tail-light flag (0x08); strobed for brake light
    bool         brake_led;   // request a red brake flash on the status LED
    ride_state_t state;
} control_out_t;

// Reset internal state (call once at startup).
void control_init(void);

// Toggle the headlight in HEADLIGHT_MANUAL mode (wired to a gesture).
void control_toggle_headlight(void);

// Compute throttle + light flags for this poll. Enforces, in order: link-loss
// failsafe, lock inhibit, brake->0; otherwise passes the rider's throttle through
// (the controller enforces kick-to-start) with curve/cap/soft-start shaping from
// g_cfg. Lights follow the configured headlight/brakelight modes.
control_out_t control_step(const inputs_t *in, const telemetry_t *t,
                           bool link_ok, bool locked);
