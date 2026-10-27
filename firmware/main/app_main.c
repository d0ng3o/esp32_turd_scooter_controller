// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// app_main.c - scooter controller firmware entry point (v1: core ride control).
//
// Brings up the peripherals and runs the single real-time ride loop: each cycle
// it samples the rider inputs, applies the control/safety state machine, and
// sends one poll to the motor controller (which also paces the loop at ~50 Hz
// and decodes the status reply). Buzzer and status LED react to the ride state.

#include "board.h"
#include "config.h"
#include "bus.h"
#include "inputs.h"
#include "control.h"
#include "range.h"
#include "gesture.h"
#include "security.h"
#include "imu.h"
#include "power.h"
#include "netcfg.h"
#include "display.h"
#include "sercon.h"
#include "buzzer.h"
#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "app";

static void ride_task(void *arg)
{
    (void)arg;
    inputs_t     in;
    telemetry_t  telem;
    uint32_t     n = 0;

    // Bus-link alerting (debounced so a flapping link doesn't chirp/log every
    // second). The throttle-inhibit failsafe is immediate in control_step(); this
    // only governs the rider chirp and the log. Start "down" to match the real
    // boot state (no status yet) so connecting the controller isn't a false
    // "restored" and losing it at boot isn't a false chirp.
    bool    link_was_ok      = false;
    bool    link_down_logged = false;   // logged a loss not yet matched by a restore
    bool    link_chirp_armed = true;    // may chirp on the next loss
    int64_t link_down_us     = 0;
    int64_t link_up_us       = 0;

    // Display power: blank the OLED to spare it. While charging, motion wakes it
    // for CHARGE_SCREEN_ON_MS; otherwise it blanks after display_timeout_s idle.
    bool    screen_on        = true;    // tracks the panel on/off state
    int64_t chg_screen_until = 0;       // charging: keep the screen on until this
    int64_t idle_screen_until =         // riding/idle: keep the screen on until this
        esp_timer_get_time() + (int64_t)g_cfg.display_timeout_s * 1000000;
    bool    was_charging     = false;   // detect the charging rising edge

    ESP_LOGI(TAG, "ride loop started");
    for (;;) {
        n++;

        inputs_sample(&in);
        bus_get_telemetry(&telem);
        bool link = bus_link_ok();

        // Activity signals. "active" (any input, wheel motion, or IMU motion)
        // keeps the scooter awake; "stationary" (wheel stopped) gates gestures.
        bool imu_mot      = imu_motion();
        bool wheel_moving = telem.valid && (telem.speed > 0);
        bool lever        = (in.throttle_cmd > 0) || in.brake_active;
        bool active       = lever || wheel_moving || imu_mot;
        bool stationary   = !wheel_moving;

        // Charging: the controller reports charge current on charger standby.
        bool charging = link && telem.valid && telem.charge_cA >= CHARGE_DETECT_cA;
        bool full     = charging && telem.soc >= CHARGE_FULL_SOC;

        // Feed the charge-based range estimator (only with a real link).
        if (link && telem.valid)
            range_update(telem.soc, telem.odometer, telem.current_cA,
                         wheel_moving, charging);

        gesture_event_t g = gesture_update(&in, stationary);
        if (g == GESTURE_CONFIG) {
            netcfg_start();      // WiFi SoftAP + config/OTA portal
        }
        netcfg_tick();           // auto-exit config mode on inactivity
        bool config_active = netcfg_is_active();

        // Pack voltage ~1 Hz (ADC2 is unavailable while the WiFi portal runs).
        if ((n % 50) == 0) inputs_sample_pack(!config_active);

        bool locked = security_update(g, active, imu_mot);

        // Throttle is inhibited while the config portal is open.
        control_out_t out = control_step(&in, &telem, link, locked || config_active);

        // Status requested on every 5th poll: N N N N S (PROTOCOL.md 5.2).
        bool want_status = (n % STATUS_EVERY_N) == 0;

        // Sends the poll AND paces this loop to ~POLL_PERIOD_MS while it pumps RX.
        bus_poll_once(out.throttle, out.headlight, out.tail_on, want_status);

        if (config_active) {
            status_led_rgb(0, 90, 90);          // cyan = config/OTA mode
        } else if (charging) {
            status_led_charging(full);          // breathing amber / solid green
        } else {
            status_led_state(out.state);
        }

        // Bus-link transitions: chirp/log once on loss, log the downtime on
        // recovery, and don't re-arm the chirp until the link has been solidly
        // up for BUS_LINK_ALERT_REARM_US (so flapping stays quiet).
        int64_t now_us = esp_timer_get_time();
        if (link && !link_was_ok) {                 // just came up
            link_up_us = now_us;
            if (link_down_logged) {
                ESP_LOGI(TAG, "bus link restored after %lld ms",
                         (long long)((now_us - link_down_us) / 1000));
                link_down_logged = false;
            }
        } else if (!link && link_was_ok) {          // just went down
            link_down_us = now_us;
            link_down_logged = true;
            ESP_LOGW(TAG, "bus link lost -> throttle inhibited");
            if (link_chirp_armed) {
                buzzer_fault();
                link_chirp_armed = false;
            }
        } else if (link && !link_chirp_armed &&
                   (now_us - link_up_us) >= BUS_LINK_ALERT_REARM_US) {
            link_chirp_armed = true;                 // solid again -> re-arm
        }
        link_was_ok = link;

        // Display power. Config portal: always on. Charging: off except for
        // CHARGE_SCREEN_ON_MS after charge-start or motion. Otherwise: on while
        // active, blanking display_timeout_s after the last activity (0 = never).
        bool chg_started = charging && !was_charging;
        was_charging = charging;
        if (chg_started) range_save();      // ride ended -> persist consumption
        if (active) idle_screen_until = now_us + (int64_t)g_cfg.display_timeout_s * 1000000;
        if (charging && (imu_mot || chg_started))
            chg_screen_until = now_us + (int64_t)CHARGE_SCREEN_ON_MS * 1000;

        bool want_screen;
        if (config_active)      want_screen = true;
        else if (charging)      want_screen = (now_us < chg_screen_until);
        else                    want_screen = (g_cfg.display_timeout_s == 0) ||
                                              (now_us < idle_screen_until);
        if (want_screen != screen_on) {
            if (want_screen) display_on(); else display_off();
            screen_on = want_screen;
        }

        // Refresh the OLED a few times per second (only when it's on).
        if (screen_on && (n % 10) == 0) {
            if (charging) {
                display_charging(telem.soc, telem.charge_cA / 100.0,
                                 inputs_pack_mv() / 1000.0, full);
            } else {
                bool   mph = (g_cfg.units == UNITS_MPH);
                double kmh = telem.speed * 0.16;     // ~0.16 km/h per speed count
                int    spd = mph ? (int)(kmh * 0.6214 + 0.5) : (int)(kmh + 0.5);
                // Adaptive range estimate (range.c); "--" (-1) without a link.
                float  range = (link && telem.valid) ? range_km(telem.soc) : -1.0f;
                if (mph && range >= 0) range *= 0.6214f;
                display_update(out.state, config_active, spd, mph,
                               telem.soc, telem.current_cA / 100.0,
                               inputs_pack_mv() / 1000.0, range);
            }
        }

        // Stage-2 idle: deep sleep after the long idle timeout. Never sleep while
        // the config portal is open or the charger is connected. Does not return
        // if it sleeps (IMU wakes it).
        power_update(active || config_active || charging);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "XIAO ESP32-C3 scooter controller fw v1 (core ride control)");

    config_init();   // load settings first; everything below reads g_cfg live
    ESP_ERROR_CHECK(buzzer_init());
    ESP_ERROR_CHECK(status_led_init());
    ESP_ERROR_CHECK(inputs_init());
    bus_selftest_tie();          // check the D6<->D7 tie while the pins are still GPIOs
    ESP_ERROR_CHECK(bus_init());
    control_init();
    range_init();        // load the learned km-per-%% rate
    gesture_init();
    security_init();
    imu_init();          // non-fatal if absent; wake-on-motion + alarm degrade off
    display_init();      // non-fatal if the OLED is absent
    power_init();

    status_led_state(RIDE_LOCKED);  // boots locked
    buzzer_boot();

    // Real-time ride loop at high priority (single core, so priority is the lever).
    xTaskCreate(ride_task, "ride", 4096, NULL, 10, NULL);

    // Interactive USB-serial console (config/calibration/status), low priority.
    sercon_init();
}
