// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// gesture.c - lever-gesture recognition.
//
// Two gestures, both requiring the brake held and the scooter stationary:
//  - Triple blip: throttle crosses high->low GEST_TRIPLE_COUNT times within a
//    window  => GESTURE_UNLOCK_TOGGLE (security decides lock vs unlock).
//  - Hold both: brake + near-full throttle held continuously for
//    GEST_CONFIG_HOLD_MS  => GESTURE_CONFIG.
// The two are mutually exclusive: the hold needs sustained high throttle, while
// each blip returns throttle to idle (which resets the hold timer).

#include "gesture.h"
#include "board.h"

#include "esp_timer.h"

#define PCT(p) ((p) * 255 / 100)

static int     s_blips;
static bool    s_in_high;
static int64_t s_window_start;
static int64_t s_config_start;     // 0 = not currently holding
static bool    s_config_latched;   // emitted; wait for release before re-arming

void gesture_init(void)
{
    s_blips = 0;
    s_in_high = false;
    s_window_start = 0;
    s_config_start = 0;
    s_config_latched = false;
}

gesture_event_t gesture_update(const inputs_t *in, bool stationary)
{
    int64_t now = esp_timer_get_time();
    gesture_event_t ev = GESTURE_NONE;
    bool gate = stationary && in->brake_active;

    // --- config/OTA: brake + near-full throttle held continuously ---
    if (gate && in->throttle_cmd >= PCT(GEST_CONFIG_HIGH_PCT)) {
        if (s_config_start == 0) s_config_start = now;
        if (!s_config_latched &&
            (now - s_config_start) >= (int64_t)GEST_CONFIG_HOLD_MS * 1000) {
            s_config_latched = true;
            ev = GESTURE_CONFIG;
        }
    } else {
        s_config_start = 0;
        s_config_latched = false;   // released -> re-armable
    }

    // --- triple blip: brake held + throttle oscillated ---
    if (!gate) {
        s_blips = 0;
        s_in_high = false;
    } else {
        if (!s_in_high && in->throttle_cmd >= PCT(GEST_BLIP_HIGH_PCT)) {
            s_in_high = true;
            if (s_blips == 0) s_window_start = now;
        } else if (s_in_high && in->throttle_cmd <= PCT(GEST_BLIP_LOW_PCT)) {
            s_in_high = false;
            if (++s_blips >= GEST_TRIPLE_COUNT) {
                s_blips = 0;
                ev = GESTURE_UNLOCK_TOGGLE;
            }
        }
        if (s_blips > 0 &&
            (now - s_window_start) > (int64_t)GEST_TRIPLE_WINDOW_MS * 1000) {
            s_blips = 0;            // window expired
            s_in_high = false;
        }
    }

    return ev;
}
