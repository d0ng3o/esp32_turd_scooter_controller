// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// display.h - 128x64 SSD1309 OLED (I2C) telemetry, rendered with LVGL.

#pragma once

#include "esp_err.h"

// Bring up the panel + LVGL. Non-fatal if the OLED is absent (returns an error
// but the rest of the firmware runs).
esp_err_t display_init(void);

// Update the readout (call a few times per second). `unit` is "km/h" or "mph".
void display_update(const char *state, int speed, const char *unit,
                    int soc, double amps, double pack_v);
