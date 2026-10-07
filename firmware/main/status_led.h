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
