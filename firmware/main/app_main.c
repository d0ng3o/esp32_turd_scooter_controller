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
#include "gesture.h"
#include "security.h"
#include "imu.h"
#include "power.h"
#include "netcfg.h"
#include "buzzer.h"
#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app";

static void ride_task(void *arg)
{
    (void)arg;
    inputs_t     in;
    telemetry_t  telem;
    ride_state_t last_state = RIDE_FAULT;
    uint32_t     n = 0;

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
        } else {
            status_led_state(out.state);
        }

        // Chirp once on entering a fault (link lost).
        if (out.state == RIDE_FAULT && last_state != RIDE_FAULT) {
            buzzer_fault();
            ESP_LOGW(TAG, "bus link lost -> throttle inhibited");
        }
        last_state = out.state;

        // Stage-2 idle: deep sleep after the long idle timeout. Never sleep while
        // the config portal is open. Does not return if it sleeps (IMU wakes it).
        power_update(active || config_active);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "XIAO ESP32-C3 scooter controller fw v1 (core ride control)");

    config_init();   // load settings first; everything below reads g_cfg live
    ESP_ERROR_CHECK(buzzer_init());
    ESP_ERROR_CHECK(status_led_init());
    ESP_ERROR_CHECK(inputs_init());
    ESP_ERROR_CHECK(bus_init());
    control_init();
    gesture_init();
    security_init();
    imu_init();          // non-fatal if absent; wake-on-motion + alarm degrade off
    power_init();

    status_led_state(RIDE_LOCKED);  // boots locked
    buzzer_boot();

    // Real-time ride loop at high priority (single core, so priority is the lever).
    xTaskCreate(ride_task, "ride", 4096, NULL, 10, NULL);
}
