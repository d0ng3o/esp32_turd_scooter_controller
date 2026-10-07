// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// power.c - stage-2 idle handling: deep sleep to conserve the pack.
//
// Stage 1 (auto-lock) lives in security.c and keeps polling. Stage 2 (here)
// fires after the longer auto_sleep_timeout_s of inactivity: it turns the lights
// off on the controller (which latches the last state once we stop polling - see
// PROTOCOL.md 6.4), arms the IMU wake interrupt, and deep-sleeps. The IMU INT1
// (D0) wakes the chip, which resets and boots locked.

#include "power.h"
#include "board.h"
#include "config.h"
#include "bus.h"
#include "imu.h"

#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "pwr";

static int64_t s_last_active_us;

void power_init(void)
{
    s_last_active_us = esp_timer_get_time();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "woke from deep sleep on IMU motion");
    }
}

static void enter_deep_sleep(void)
{
    ESP_LOGW(TAG, "idle %u s -> deep sleep", g_cfg.auto_sleep_timeout_s);

    // The controller latches the last light state once we stop polling, so send
    // a few final polls with throttle 0 and all light flags cleared.
    for (int i = 0; i < 5; i++) {
        bus_poll_once(0, /*headlight=*/false, /*tail_on=*/false, /*want_status=*/false);
    }

    imu_arm_wake();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_IMU_INT, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();   // resets the chip on wake (boots locked)
}

void power_update(bool active)
{
    int64_t now = esp_timer_get_time();
    if (active) {
        s_last_active_us = now;
        return;
    }
    if (g_cfg.auto_sleep_timeout_s == 0) return;    // sleep disabled

    if ((now - s_last_active_us) > (int64_t)g_cfg.auto_sleep_timeout_s * 1000000) {
        if (imu_present()) {
            enter_deep_sleep();
        } else {
            // No IMU means nothing can wake us - staying awake beats a dead scooter.
            ESP_LOGW(TAG, "sleep due but no IMU to wake; staying awake");
            s_last_active_us = now;
        }
    }
}
