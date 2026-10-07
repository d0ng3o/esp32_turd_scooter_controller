// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// display.c - SSD1309 128x64 OLED over I2C, driven with LVGL via esp_lvgl_port.
// The SSD1306 panel driver is command-compatible with the SSD1309. Shares the
// I2C bus (i2cbus) with the IMU. Layout: big speed, SoC, state, current, pack V.

#include "display.h"
#include "board.h"
#include "i2cbus.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "oled";

#define OLED_ADDR 0x3C
#define H_RES     128
#define V_RES     64

static lv_display_t *s_disp;
static lv_obj_t *s_speed, *s_unit, *s_soc, *s_state, *s_amp;

esp_err_t display_init(void)
{
    if (i2cbus_init() != ESP_OK) return ESP_FAIL;

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    if (esp_lcd_new_panel_io_i2c(i2cbus_handle(), &io_cfg, &io) != ESP_OK) {
        ESP_LOGW(TAG, "panel IO init failed");
        return ESP_FAIL;
    }

    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_ssd1306_config_t ssd = { .height = V_RES };
    const esp_lcd_panel_dev_config_t pcfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = (void *)&ssd,
    };
    if (esp_lcd_new_panel_ssd1306(io, &pcfg, &panel) != ESP_OK) {
        ESP_LOGW(TAG, "SSD1306/09 panel init failed (OLED absent?)");
        return ESP_FAIL;
    }
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_disp_on_off(panel, true);

    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&pc);

    const lvgl_port_display_cfg_t dc = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = H_RES * V_RES,
        .double_buffer = false,
        .hres = H_RES,
        .vres = V_RES,
        .monochrome = true,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) return ESP_FAIL;

    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_display_get_screen_active(s_disp);
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        s_speed = lv_label_create(scr);
        lv_obj_set_style_text_color(s_speed, lv_color_white(), 0);
        lv_obj_set_style_text_font(s_speed, &lv_font_montserrat_28, 0);
        lv_obj_align(s_speed, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_label_set_text(s_speed, "--");

        s_unit = lv_label_create(scr);
        lv_obj_set_style_text_color(s_unit, lv_color_white(), 0);
        lv_obj_align(s_unit, LV_ALIGN_TOP_LEFT, 56, 12);
        lv_label_set_text(s_unit, "km/h");

        s_soc = lv_label_create(scr);
        lv_obj_set_style_text_color(s_soc, lv_color_white(), 0);
        lv_obj_align(s_soc, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_label_set_text(s_soc, "--%");

        s_amp = lv_label_create(scr);
        lv_obj_set_style_text_color(s_amp, lv_color_white(), 0);
        lv_obj_align(s_amp, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_label_set_text(s_amp, "-- A");

        s_state = lv_label_create(scr);
        lv_obj_set_style_text_color(s_state, lv_color_white(), 0);
        lv_obj_align(s_state, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_label_set_text(s_state, "BOOT");

        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "OLED up (128x64 SSD1309 via LVGL)");
    return ESP_OK;
}

void display_update(const char *state, int speed, const char *unit,
                    int soc, double amps, double pack_v)
{
    if (!s_disp) return;
    if (!lvgl_port_lock(10)) return;
    char b[24];
    snprintf(b, sizeof(b), "%d", speed);           lv_label_set_text(s_speed, b);
    lv_label_set_text(s_unit, unit);
    snprintf(b, sizeof(b), "%d%%", soc);           lv_label_set_text(s_soc, b);
    snprintf(b, sizeof(b), "%.1f A / %.0fV", amps, pack_v); lv_label_set_text(s_amp, b);
    lv_label_set_text(s_state, state);
    lvgl_port_unlock();
}
