// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// bus.h - one-wire open-drain UART link to the motor controller.

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "protocol.h"

// Configure UART1 on the D6/D7 pins as an open-drain single-wire bus.
esp_err_t bus_init(void);

// Send one poll and then pump the RX line for ~POLL_PERIOD_MS, filtering our own
// echo and decoding any status frame. This call provides the 50 Hz pacing, so
// the ride loop calls it once per cycle. want_status sets the status-request
// flag; tail_on/headlight set the light flags (tail is normally true, but is
// strobed by the caller for the experimental brake light).
void bus_poll_once(uint8_t throttle, bool headlight, bool tail_on, bool want_status);

// Copy the latest decoded telemetry into *out. Returns true if a status frame
// has ever been decoded (out->valid), false otherwise.
bool bus_get_telemetry(telemetry_t *out);

// True while a valid status has been seen within BUS_LINK_TIMEOUT_US.
bool bus_link_ok(void);
