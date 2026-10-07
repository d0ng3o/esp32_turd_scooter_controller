// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// status_led.c - one WS2812B on D8, driven via the espressif/led_strip RMT
// component. Colour encodes the ride state.

#include "status_led.h"
#include "board.h"
#include "config.h"

#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "led";

static led_strip_handle_t s_strip;
static int s_last = -1;

esp_err_t status_led_init(void)
{
    const led_strip_config_t strip_cfg = {
        .strip_gpio_num   = PIN_WS2812,
        .max_leds         = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,   // led_strip 2.5.x API
        .led_model        = LED_MODEL_WS2812,
        .flags            = { .invert_out = false },
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,   // 10 MHz
        .flags = { .with_dma = false },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return err;
    }
    led_strip_clear(s_strip);
    return ESP_OK;
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    uint16_t br = g_cfg.led_brightness;                 // scale by config
    led_strip_set_pixel(s_strip, 0, r * br / 255, g * br / 255, b * br / 255);
    led_strip_refresh(s_strip);
}

void status_led_state(ride_state_t state)
{
    if ((int)state == s_last) return;
    s_last = (int)state;

    switch (state) {
        case RIDE_FAULT:   set_rgb(255,   0,   0); break;  // red
        case RIDE_LOCKED:  set_rgb(160,   0, 160); break;  // purple
        case RIDE_IDLE:    set_rgb(  0,   0, 160); break;  // blue
        case RIDE_READY:   set_rgb(  0, 180,  40); break;  // teal-green
        case RIDE_RIDING:  set_rgb(  0, 255,   0); break;  // green
        case RIDE_BRAKING: set_rgb(255, 120,   0); break;  // amber
        default:           set_rgb( 80,  80,  80); break;
    }
}
