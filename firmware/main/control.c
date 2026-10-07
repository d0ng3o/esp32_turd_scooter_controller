// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// control.c - ride state machine with the safety interlocks from PROTOCOL.md 6.
//
// Throttle is a SPEED setpoint. The controller itself only engages the motor
// after the wheel is kicked into motion and the throttle rises from idle; this
// firmware mirrors that so we never command speed into a standstill, and adds a
// hard failsafe: any lost bus link or engaged brake forces the throttle byte to 0.

#include "control.h"
#include "board.h"

// Persistent interlock state across poll cycles.
static bool s_armed;        // rider released throttle after motion -> may apply

void control_init(void)
{
    s_armed = false;
}

control_out_t control_step(const inputs_t *in, const telemetry_t *t, bool link_ok)
{
    control_out_t out = { .throttle = 0, .headlight = false, .state = RIDE_FAULT };

    // 1. Failsafe: no controller link -> no motion.
    if (!link_ok) {
        s_armed = false;
        out.state = RIDE_FAULT;
        return out;
    }

    bool moving = t->valid && (t->speed > 0);
    out.headlight = moving;                     // headlight while the wheel turns

    // 2. Brake overrides everything: command speed 0 (PROTOCOL.md 6.3).
    if (in->brake_active) {
        out.throttle = 0;
        out.state = RIDE_BRAKING;
        return out;                             // keep s_armed; resume on release if still moving
    }

    // 3. Stopped: no throttle, and require a fresh kick before arming again.
    if (!moving) {
        s_armed = false;
        out.throttle = 0;
        out.state = RIDE_IDLE;
        return out;
    }

    // 4. Moving: arm on a released throttle, then pass it through (kick-to-start).
    if (in->throttle_cmd == 0) {
        s_armed = true;                         // released after motion -> ready
        out.throttle = 0;
        out.state = RIDE_READY;
    } else if (s_armed) {
        out.throttle = in->throttle_cmd;
        out.state = RIDE_RIDING;
    } else {
        // Moving but throttle was held through the stop: ignore until released,
        // matching the controller's own interlock.
        out.throttle = 0;
        out.state = RIDE_READY;
    }
    return out;
}
