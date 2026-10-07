// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// power.h - battery-saving deep sleep (stage 2 of the idle model).

#pragma once

#include <stdbool.h>

void power_init(void);

// Call once per ride-loop cycle. `active` = any lever input, wheel motion, or
// IMU motion. After auto_sleep_timeout_s of inactivity it clears the lights and
// enters deep sleep (IMU INT wakes it). Does not return if it sleeps.
void power_update(bool active);
