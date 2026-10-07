// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// control.h - ride state machine + safety interlocks.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "inputs.h"
#include "protocol.h"

typedef enum {
    RIDE_FAULT = 0,   // no bus link (or other fault): throttle forced 0
    RIDE_IDLE,        // link ok, wheel stopped: waiting for a kick
    RIDE_READY,       // kicked + throttle released: armed to accept throttle
    RIDE_RIDING,      // delivering throttle to the controller
    RIDE_BRAKING,     // brake engaged: throttle forced 0
} ride_state_t;

typedef struct {
    uint8_t      throttle;    // 0..255 byte to send in the poll
    bool         headlight;   // headlight flag (set while moving)
    ride_state_t state;
} control_out_t;

// Reset internal state (call once at startup).
void control_init(void);

// Compute the throttle command + flags for this poll cycle from the rider inputs
// and the latest telemetry. Enforces: link-loss failsafe, brake->0, and the
// kick-to-start interlock (PROTOCOL.md 6.1-6.3).
control_out_t control_step(const inputs_t *in, const telemetry_t *t, bool link_ok);
