// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// security.h - lock state machine: gesture lock/unlock, auto-lock on idle,
// and the motion alarm while locked.

#pragma once

#include <stdbool.h>
#include "gesture.h"

// Boots LOCKED (safe default: the scooter must be unlocked before it will ride).
void security_init(void);

// Advance the lock state machine for this cycle and return the lock state
// (true = locked). `active` = any lever/wheel/IMU activity (resets the auto-lock
// timer); `motion` = IMU motion (only matters while locked, for the alarm).
bool security_update(gesture_event_t g, bool active, bool motion);

bool security_is_locked(void);
