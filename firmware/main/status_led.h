// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// status_led.h - single WS2812 status LED (D8).

#pragma once

#include "esp_err.h"
#include "control.h"

esp_err_t status_led_init(void);

// Set the LED to reflect the ride state (idempotent; only refreshes on change).
void status_led_state(ride_state_t state);

// Set an explicit colour (e.g. config-mode indicator). Forces the next
// status_led_state() call to refresh.
void status_led_rgb(uint8_t r, uint8_t g, uint8_t b);

// Bring-up test: drive the pixel at the given colour WITHOUT the config
// brightness scaling (full drive), so a dim/zero led_brightness can't hide a
// working LED. Holds the LED (the ride loop stops updating it) until
// status_led_auto(). Used by the `led` console command.
void status_led_raw(uint8_t r, uint8_t g, uint8_t b);

// Release the manual hold from status_led_raw(); the ride state drives again.
void status_led_auto(void);

// Blank the pixel (send an off frame). Used before deep sleep so the WS2812
// doesn't hold its last colour while the board is asleep.
void status_led_off(void);
