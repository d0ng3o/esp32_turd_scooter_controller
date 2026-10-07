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
#include "buzzer.h"
#include "status_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app";

// Entered by the brake+full-throttle-20s gesture. The WiFi/web/OTA phase will
// replace this stub with the real SoftAP + config portal + OTA flow.
static void enter_config_mode(void)
{
    ESP_LOGW(TAG, "config/OTA mode requested (WiFi phase not yet implemented)");
    buzzer_pattern(2200, 100, 80, 3);
    // TODO(wifi phase): start SoftAP + HTTP config UI + OTA; suspend ride loop
    // (throttle inhibited) while active; exit on timeout or gesture.
}

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

        // Stationary from the controller's speed for now; the IMU phase will
        // refine this (and feed the motion alarm).
        bool stationary = !(telem.valid && telem.speed > 0);

        gesture_event_t g = gesture_update(&in, stationary);
        if (g == GESTURE_CONFIG) {
            enter_config_mode();
        }
        bool locked = security_update(g, &in, stationary, /*motion=*/false);

        control_out_t out = control_step(&in, &telem, link, locked);

        // Status requested on every 5th poll: N N N N S (PROTOCOL.md 5.2).
        bool want_status = (n % STATUS_EVERY_N) == 0;

        // Sends the poll AND paces this loop to ~POLL_PERIOD_MS while it pumps RX.
        bus_poll_once(out.throttle, out.headlight, out.tail_on, want_status);

        status_led_state(out.state);

        // Chirp once on entering a fault (link lost).
        if (out.state == RIDE_FAULT && last_state != RIDE_FAULT) {
            buzzer_fault();
            ESP_LOGW(TAG, "bus link lost -> throttle inhibited");
        }
        last_state = out.state;
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

    status_led_state(RIDE_LOCKED);  // boots locked
    buzzer_boot();

    // Real-time ride loop at high priority (single core, so priority is the lever).
    xTaskCreate(ride_task, "ride", 4096, NULL, 10, NULL);
}
