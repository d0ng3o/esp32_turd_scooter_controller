// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// inputs.h - throttle and brake analog inputs (ADC1).

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int     throttle_raw;   // last filtered raw ADC (0..4095)
    int     brake_raw;
    uint8_t throttle_cmd;   // mapped + deadzoned, 0..255
    bool    brake_active;   // brake past BRAKE_ACTIVE_PCT of travel
} inputs_t;

// Initialise the ADC oneshot units and channels (ADC1 throttle/brake, ADC2 pack).
esp_err_t inputs_init(void);

// Sample throttle + brake, filter, and map into *out.
void inputs_sample(inputs_t *out);

// Read the pack voltage (ADC2) and update the cached value - only when `allowed`
// (ADC2 is unavailable while WiFi runs; pass false then to keep the cache).
void inputs_sample_pack(bool allowed);

// Last cached pack voltage in millivolts (0 if never read).
uint32_t inputs_pack_mv(void);
