// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// buzzer.h - passive buzzer via LEDC PWM. All calls are non-blocking (a small
// task plays queued patterns).

#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t buzzer_init(void);

// Queue a pattern: `count` beeps of `freq` Hz, each `on_ms` on then `off_ms` off.
void buzzer_pattern(uint16_t freq, uint16_t on_ms, uint16_t off_ms, uint8_t count);

// Convenience patterns.
void buzzer_boot(void);    // single short chirp at power-up
void buzzer_fault(void);   // triple warning beep
