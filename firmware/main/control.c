// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// control.c - ride state machine, ride-feel shaping (throttle cap, curve,
// soft-start) and light-flag logic.
//
// Throttle is a SPEED setpoint. Kick-to-start (the motor engages only once the
// wheel is rolling and the throttle rises from idle) is enforced by the MOTOR
// CONTROLLER itself (PROTOCOL.md 6.2) and cannot be changed, so the firmware
// just passes the rider's throttle through - no duplicate gate here. The hard
// failsafes remain: lost link, lock, or brake all force the throttle byte to 0.

#include "control.h"
#include "board.h"
#include "config.h"

#include "esp_timer.h"

// Persistent state across poll cycles.
static uint8_t s_out_slew;           // last emitted throttle (for soft-start ramp)
static bool    s_headlight_manual;   // toggle state for HEADLIGHT_MANUAL
static int64_t s_moving_until_us;    // headlight auto-off delay bookkeeping

void control_init(void)
{
    s_out_slew = 0;
    s_headlight_manual = false;
    s_moving_until_us = 0;
}

void control_toggle_headlight(void)
{
    s_headlight_manual = !s_headlight_manual;
}

// Headlight flag for this cycle, per configured mode. AUTO keeps it on for
// headlight_off_delay_ms after the wheel stops.
static bool headlight_flag(bool moving)
{
    switch (g_cfg.headlight_mode) {
        case HEADLIGHT_ON:     return true;
        case HEADLIGHT_OFF:    return false;
        case HEADLIGHT_MANUAL: return s_headlight_manual;
        case HEADLIGHT_AUTO:
        default:
            if (moving) {
                s_moving_until_us = esp_timer_get_time()
                                    + (int64_t)g_cfg.headlight_off_delay_ms * 1000;
                return true;
            }
            return esp_timer_get_time() < s_moving_until_us;
    }
}

// Tail flag; strobes ~2 Hz while braking when BRAKELIGHT_STROBE_TAIL is set.
static bool tail_flag(bool braking)
{
    if (braking && g_cfg.brakelight_mode == BRAKELIGHT_STROBE_TAIL) {
        return (esp_timer_get_time() / 250000) & 1;   // 250 ms on/off
    }
    return true;   // tail is otherwise always on (original behaviour)
}

// Apply throttle curve + cap to a 0..255 input.
static uint8_t shape_target(uint8_t raw)
{
    uint8_t curved = raw;
    if (g_cfg.throttle_curve == THROTTLE_PROGRESSIVE) {
        curved = (uint8_t)(((uint16_t)raw * raw) / 255);   // square law
    }
    return (uint8_t)(((uint16_t)curved * g_cfg.throttle_cap) / 255);
}

// Soft-start slew: ramp up toward target at a bounded rate, drop instantly.
static uint8_t slew_to(uint8_t target)
{
    if (target <= s_out_slew) {           // release/brake: immediate (safety)
        s_out_slew = target;
    } else if (g_cfg.soft_start_ms == 0) {
        s_out_slew = target;
    } else {
        int step = g_cfg.throttle_cap * POLL_PERIOD_MS / g_cfg.soft_start_ms;
        if (step < 1) step = 1;
        int next = s_out_slew + step;
        s_out_slew = (next > target) ? target : (uint8_t)next;
    }
    return s_out_slew;
}

control_out_t control_step(const inputs_t *in, const telemetry_t *t,
                           bool link_ok, bool locked)
{
    bool moving   = t->valid && link_ok && (t->speed > 0);
    bool braking  = in->brake_active;

    control_out_t out = {
        .throttle  = 0,
        .headlight = headlight_flag(moving),
        .tail_on   = tail_flag(braking),
        .brake_led = braking && (g_cfg.brakelight_mode == BRAKELIGHT_WS2812),
        .state     = RIDE_FAULT,
    };

    // 1. Failsafe: no controller link -> no motion.
    if (!link_ok) {
        s_out_slew = 0;
        out.state = RIDE_FAULT;
        return out;
    }

    // 2. Locked: throttle inhibited until unlocked.
    if (locked) {
        s_out_slew = 0;
        out.state = RIDE_LOCKED;
        return out;
    }

    // 3. Brake overrides: command speed 0 (PROTOCOL.md 6.3).
    if (braking) {
        s_out_slew = 0;
        out.state = RIDE_BRAKING;
        return out;
    }

    // 4. Pass the rider's throttle through (the controller enforces kick-to-start).
    out.throttle = slew_to(shape_target(in->throttle_cmd));
    out.state = out.throttle ? RIDE_RIDING : RIDE_IDLE;
    return out;
}
