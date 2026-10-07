// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// gesture.h - detect lever gestures from the throttle/brake input stream.
// Evaluated only while the scooter is stationary.

#pragma once

#include "inputs.h"

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_UNLOCK_TOGGLE,   // brake held + triple throttle blip -> toggle lock
    GESTURE_CONFIG,          // brake + full throttle held 20 s -> config/OTA mode
} gesture_event_t;

void gesture_init(void);

// Feed one input sample (call each ride-loop cycle). Returns the gesture that
// completed this cycle, or GESTURE_NONE. `stationary` must be true for any
// gesture to be recognised.
gesture_event_t gesture_update(const inputs_t *in, bool stationary);
