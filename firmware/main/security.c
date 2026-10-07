// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// security.c - lock/unlock state, auto-lock on idle, motion alarm when locked.

#include "security.h"
#include "config.h"
#include "buzzer.h"

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "sec";

static bool    s_locked;
static int64_t s_last_activity_us;
static int64_t s_last_alarm_us;

void security_init(void)
{
    s_locked = true;                       // safe default
    s_last_activity_us = esp_timer_get_time();
    s_last_alarm_us = 0;
    ESP_LOGI(TAG, "locked at boot");
}

bool security_is_locked(void)
{
    return s_locked;
}

static void set_locked(bool v)
{
    if (v == s_locked) return;
    s_locked = v;
    if (v) {
        buzzer_pattern(1500, 200, 0, 1);   // low beep = locked
    } else {
        buzzer_pattern(2700, 60, 60, 2);   // two chirps = unlocked
    }
    ESP_LOGI(TAG, "%s", v ? "LOCKED" : "UNLOCKED");
}

bool security_update(gesture_event_t g, const inputs_t *in,
                     bool stationary, bool motion)
{
    int64_t now = esp_timer_get_time();

    if (in->throttle_cmd > 0 || in->brake_active) {
        s_last_activity_us = now;
    }

    if (g == GESTURE_UNLOCK_TOGGLE) {
        set_locked(!s_locked);
        s_last_activity_us = now;
    }

    // Auto-lock after inactivity while stopped.
    if (!s_locked && g_cfg.auto_lock_timeout_s > 0 && stationary &&
        (now - s_last_activity_us) > (int64_t)g_cfg.auto_lock_timeout_s * 1000000) {
        set_locked(true);
    }

    // Motion alarm while locked (IMU-driven; rate-limited to ~1 Hz).
    if (s_locked && g_cfg.motion_alarm && motion) {
        if (now - s_last_alarm_us > 1000000) {
            s_last_alarm_us = now;
            buzzer_pattern(3000, 150, 100, 3);
            ESP_LOGW(TAG, "motion detected while locked");
        }
    }

    return s_locked;
}
