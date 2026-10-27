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

// ---- bring-up diagnostics ---------------------------------------------------
// Running byte/frame counters on the wire, for the `bus` console command. They
// separate the three failure modes of a dead link: nothing echoes (TX/pin
// problem), only our echo comes back (controller silent/unwired), or foreign
// bytes arrive but never frame (baud/level/checksum mismatch).
typedef struct {
    uint32_t tx_total;        // bytes we transmitted
    uint32_t rx_total;        // raw bytes read back
    uint32_t echo_total;      // rx bytes consumed as our own echo
    uint32_t foreign_total;   // rx bytes that were NOT our echo (controller/noise)
    uint32_t frames_ok;       // valid status frames decoded
    uint32_t sof_seen;        // 0x3A start-of-frame bytes among foreign bytes
    uint8_t  last[16];        // most-recent foreign bytes (oldest..newest)
    uint8_t  last_len;        // valid entries in last[]
    int8_t   tie_ok;          // D6<->D7 tie self-test: -1 unknown, 0 open, 1 ok
    int8_t   echo_nat;        // boot echo probe, native uart_set_pin routing: -1 n/a, else bytes
    int8_t   echo_od;         // boot echo probe, open-drain drive: -1 n/a, else bytes
    int8_t   echo_lb;         // boot echo probe, UART internal loopback (no pads)
} bus_debug_t;

void bus_get_debug(bus_debug_t *out);
void bus_reset_debug(void);

// GPIO-level check of the external D6<->D7 tie. Call ONCE before bus_init(),
// while the pins are still plain GPIOs: it drives TX and reads RX (RX forced low
// with a pulldown, so a high read can only arrive through the tie). Returns true
// if the wire is intact. Separates a broken tie/solder joint (returns false)
// from a firmware/UART-routing problem (returns true but the link still fails).
bool bus_selftest_tie(void);
