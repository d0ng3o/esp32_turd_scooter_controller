// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// display.h - 128x64 SSD1309 OLED (I2C) telemetry, rendered with LVGL.

#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Bring up the panel + LVGL. Non-fatal if the OLED is absent (returns an error
// but the rest of the firmware runs).
esp_err_t display_init(void);

// Ride readout (call a few times per second). `state` is a ride_state_t (passed
// as int to keep this header free of control.h); `config_active` shows the WiFi
// icon; `mph` selects units; `range` is the estimated remaining distance.
void display_update(int state, bool config_active, int speed, bool mph,
                    int soc, double amps, double pack_v, float range);

// Turn the panel off (blank) ahead of deep sleep / while charging. The
// framebuffer is kept, so the next update restores the readout.
void display_off(void);

// Re-enable the panel after a display_off() without a reboot (charging-screen
// wake on motion).
void display_on(void);

// Charging readout: big SoC %, an animated filling battery, charge current and
// pack voltage.
void display_charging(int soc, double charge_a, double pack_v, bool full);
