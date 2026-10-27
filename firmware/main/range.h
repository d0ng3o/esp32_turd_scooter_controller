// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// range.h - charge-based adaptive range estimate. Learns consumption (Ah/km) by
// coulomb-counting pack current over distance while riding; range = SoC% x
// capacity_Ah / Ah_per_km. Seeded from range_full_km, persisted to NVS. See
// range.c.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Load the learned consumption from NVS (or seed it from capacity/range_full_km).
void range_init(void);

// Feed one telemetry sample each ride-loop cycle (only with a valid link, so the
// values are real). `cur_cA` is the pack current (0.01 A), `moving` gates
// learning to actual riding, `charging` pauses it.
void range_update(int soc, uint32_t odo, int cur_cA, bool moving, bool charging);

// Estimated remaining range in km for the given SoC (-1 if unknown).
float range_km(int soc);

// Learned consumption (Ah/km), for diagnostics.
float range_ah_per_km(void);

// Persist the learned consumption to NVS (call at ride-end: sleep / charge start).
void range_save(void);

// Re-seed the estimate and clear the persisted value (console `range reset`).
void range_reset(void);
