// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// control.c - ride state machine with the safety interlocks from PROTOCOL.md 6,
// plus ride-feel shaping (throttle cap, curve, soft-start) and light-flag logic.
//
// Throttle is a SPEED setpoint; the controller only engages after the wheel is
// kicked and the throttle rises from idle. This firmware mirrors that and adds
// hard failsafes: lost link, lock, or brake all force the throttle byte to 0.

#include "control.h"
#include "board.h"
#include "config.h"

#include "esp_timer.h"

// Persistent state across poll cycles.
static bool    s_armed;              // released throttle after motion -> may apply
static uint8_t s_out_slew;           // last emitted throttle (for soft-start ramp)
static bool    s_headlight_manual;   // toggle state for HEADLIGHT_MANUAL
static int64_t s_moving_until_us;    // headlight auto-off delay bookkeeping

void control_init(void)
{
    s_armed = false;
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
        s_armed = false;
        s_out_slew = 0;
        out.state = RIDE_FAULT;
        return out;
    }

    // 2. Locked: throttle inhibited until unlocked.
    if (locked) {
        s_armed = false;
        s_out_slew = 0;
        out.state = RIDE_LOCKED;
        return out;
    }

    // 3. Brake overrides: command speed 0 (PROTOCOL.md 6.3).
    if (braking) {
        s_out_slew = 0;
        out.state = RIDE_BRAKING;
        return out;                          // keep s_armed; resume on release if moving
    }

    // 4. Stopped: no throttle; require a fresh kick before arming again.
    if (g_cfg.kick_to_start && !moving) {
        s_armed = false;
        s_out_slew = 0;
        out.state = RIDE_IDLE;
        return out;
    }

    // 5. Moving (or kick-to-start disabled): arm on release, then pass through.
    uint8_t target = 0;
    if (!g_cfg.kick_to_start) {
        target = in->throttle_cmd;
        out.state = in->throttle_cmd ? RIDE_RIDING : RIDE_READY;
    } else if (in->throttle_cmd == 0) {
        s_armed = true;
        out.state = RIDE_READY;
    } else if (s_armed) {
        target = in->throttle_cmd;
        out.state = RIDE_RIDING;
    } else {
        out.state = RIDE_READY;              // held through the stop: wait for release
    }

    out.throttle = slew_to(shape_target(target));
    return out;
}
