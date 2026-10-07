// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// security.h - lock state machine: gesture lock/unlock, auto-lock on idle,
// and the motion alarm while locked.

#pragma once

#include <stdbool.h>
#include "inputs.h"
#include "gesture.h"

// Boots LOCKED (safe default: the scooter must be unlocked before it will ride).
void security_init(void);

// Advance the lock state machine for this cycle and return the current lock
// state (true = locked). `motion` comes from the IMU (pass false until the IMU
// phase lands); it only matters while locked, for the alarm.
bool security_update(gesture_event_t g, const inputs_t *in,
                     bool stationary, bool motion);

bool security_is_locked(void);
