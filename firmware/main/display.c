// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// display.c - SSD1309 128x64 OLED over I2C, driven with LVGL via esp_lvgl_port.
// The SSD1306 panel driver is command-compatible with the SSD1309. Shares the
// I2C bus (i2cbus) with the IMU.
//
// Two screens: a banded ride readout (state icon + SoC bar, big speed, and a
// current/volts/range footer) and a charging screen (big SoC, an animated
// filling battery, and charge current). Light/dark mode is a whole-panel colour
// inversion (esp_lcd_panel_invert_color), selected by g_cfg.display_invert.

#include "display.h"
#include "board.h"
#include "config.h"
#include "control.h"
#include "i2cbus.h"
#include "icons.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "oled";

#define OLED_ADDR 0x3C
#define H_RES     128
#define V_RES     64

static lv_display_t          *s_disp;
static esp_lcd_panel_handle_t s_panel;
static int                    s_invert = -1;   // last applied invert (-1 = unset)

// Ride screen objects.
static lv_obj_t *s_ride;
static lv_obj_t *s_st_lbl, *s_st_img, *s_soc, *s_speed, *s_unit, *s_amp, *s_volt, *s_range;
// Charging screen objects.
static lv_obj_t *s_chg;
static lv_obj_t *s_chg_title, *s_chg_soc, *s_chg_cur;

// --- helpers ----------------------------------------------------------------

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font,
                          lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static lv_obj_t *mk_divider(lv_obj_t *parent, int y)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, H_RES, 1);
    lv_obj_set_style_bg_color(d, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, y);
    return d;
}

static void style_screen(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
}

// Battery glyph for a SoC percentage.
static const char *batt_symbol(int soc)
{
    if (soc >= 88) return LV_SYMBOL_BATTERY_FULL;
    if (soc >= 63) return LV_SYMBOL_BATTERY_3;
    if (soc >= 38) return LV_SYMBOL_BATTERY_2;
    if (soc >= 13) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

// State -> status symbol; returns NULL for LOCKED (drawn with the lock image).
static const char *state_symbol(int state, bool config)
{
    if (config) return LV_SYMBOL_WIFI;
    switch (state) {
        case RIDE_FAULT:   return LV_SYMBOL_WARNING;
        case RIDE_LOCKED:  return NULL;
        case RIDE_BRAKING: return LV_SYMBOL_STOP;
        case RIDE_RIDING:  return LV_SYMBOL_PLAY;
        case RIDE_IDLE:
        case RIDE_READY:
        default:           return LV_SYMBOL_PAUSE;
    }
}

static void apply_invert(void)
{
    int want = g_cfg.display_invert ? 1 : 0;
    if (want != s_invert && s_panel) {
        esp_lcd_panel_invert_color(s_panel, want);   // whole-panel light/dark
        s_invert = want;
    }
}

// --- init -------------------------------------------------------------------

static void build_ride_screen(void)
{
    s_ride = lv_obj_create(NULL);
    style_screen(s_ride);

    // Top bar: state icon (left) + battery/SoC (right).
    s_st_lbl = mk_label(s_ride, &lv_font_montserrat_14, LV_ALIGN_TOP_LEFT, 2, 0);
    lv_label_set_text(s_st_lbl, LV_SYMBOL_PAUSE);
    s_st_img = lv_image_create(s_ride);
    lv_image_set_src(s_st_img, &icon_lock);
    lv_obj_set_style_image_recolor(s_st_img, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(s_st_img, LV_OPA_COVER, 0);
    lv_obj_align(s_st_img, LV_ALIGN_TOP_LEFT, 2, 0);
    lv_obj_add_flag(s_st_img, LV_OBJ_FLAG_HIDDEN);
    s_soc = mk_label(s_ride, &lv_font_montserrat_14, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_label_set_text(s_soc, LV_SYMBOL_BATTERY_FULL " --%");

    mk_divider(s_ride, 17);

    // Big speed, centred in the band; the unit sits beside its baseline (aligned
    // to the speed label in display_update, since the number's width varies).
    s_speed = mk_label(s_ride, &lv_font_montserrat_36, LV_ALIGN_CENTER, -8, 1);
    lv_label_set_text(s_speed, "--");
    s_unit = mk_label(s_ride, &lv_font_montserrat_12, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_unit, "km/h");

    mk_divider(s_ride, 49);

    // Footer: current / pack volts / range.
    s_amp   = mk_label(s_ride, &lv_font_montserrat_12, LV_ALIGN_BOTTOM_LEFT,  0, 0);
    lv_label_set_text(s_amp, "--A");
    s_volt  = mk_label(s_ride, &lv_font_montserrat_12, LV_ALIGN_BOTTOM_MID,   0, 0);
    lv_label_set_text(s_volt, "--V");
    s_range = mk_label(s_ride, &lv_font_montserrat_12, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_label_set_text(s_range, "--km");
}

static void build_charge_screen(void)
{
    s_chg = lv_obj_create(NULL);
    style_screen(s_chg);

    s_chg_title = mk_label(s_chg, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text(s_chg_title, LV_SYMBOL_CHARGE " CHARGING");

    mk_divider(s_chg, 17);

    s_chg_soc = mk_label(s_chg, &lv_font_montserrat_36, LV_ALIGN_CENTER, 0, 2);
    lv_label_set_text(s_chg_soc, "--%");

    s_chg_cur = mk_label(s_chg, &lv_font_montserrat_12, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(s_chg_cur, "-- A");
}

esp_err_t display_init(void)
{
    if (i2cbus_init() != ESP_OK) return ESP_FAIL;

    // Only bring up the panel + LVGL if the OLED actually ACKs. Otherwise the
    // LVGL flush task would spin on failing I2C writes and starve the system.
    if (i2c_master_probe(i2cbus_handle(), OLED_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no OLED at 0x%02X - display disabled", OLED_ADDR);
        return ESP_OK;
    }

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
    s_panel = panel;

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
        build_ride_screen();
        build_charge_screen();
        lv_screen_load(s_ride);
        lvgl_port_unlock();
    }
    apply_invert();
    ESP_LOGI(TAG, "OLED up (128x64 SSD1309, LVGL %d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
    return ESP_OK;
}

// --- runtime ----------------------------------------------------------------

void display_off(void)
{
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, false);
}

void display_on(void)
{
    if (s_panel) esp_lcd_panel_disp_on_off(s_panel, true);
}

void display_update(int state, bool config_active, int speed, bool mph,
                    int soc, double amps, double pack_v, int range)
{
    if (!s_disp) return;
    apply_invert();
    if (!lvgl_port_lock(10)) return;
    if (lv_screen_active() != s_ride) lv_screen_load(s_ride);

    const char *sym = state_symbol(state, config_active);
    if (sym) {
        lv_label_set_text(s_st_lbl, sym);
        lv_obj_remove_flag(s_st_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_st_img, LV_OBJ_FLAG_HIDDEN);
    } else {                                   // LOCKED -> padlock image
        lv_obj_add_flag(s_st_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_st_img, LV_OBJ_FLAG_HIDDEN);
    }

    char b[28];
    snprintf(b, sizeof(b), "%s %d%%", batt_symbol(soc), soc);  lv_label_set_text(s_soc, b);
    snprintf(b, sizeof(b), "%d", speed);                       lv_label_set_text(s_speed, b);
    lv_label_set_text(s_unit, mph ? "mph" : "km/h");
    // Sit the unit just right of the (variable-width) speed number, vertically
    // centred on it.
    lv_obj_update_layout(s_ride);
    lv_obj_align_to(s_unit, s_speed, LV_ALIGN_OUT_RIGHT_MID, 3, 0);
    snprintf(b, sizeof(b), "%.1fA", amps);                     lv_label_set_text(s_amp, b);
    snprintf(b, sizeof(b), "%.1fV", pack_v);                   lv_label_set_text(s_volt, b);
    if (range < 0) snprintf(b, sizeof(b), "--%s", mph ? "mi" : "km");
    else           snprintf(b, sizeof(b), "%d%s", range, mph ? "mi" : "km");
    lv_label_set_text(s_range, b);
    lvgl_port_unlock();
}

void display_charging(int soc, double charge_a, double pack_v, bool full)
{
    if (!s_disp) return;
    apply_invert();
    if (!lvgl_port_lock(10)) return;
    if (lv_screen_active() != s_chg) lv_screen_load(s_chg);

    char b[28];
    if (full) {
        lv_label_set_text(s_chg_title, LV_SYMBOL_BATTERY_FULL " FULL");
    } else {
        // Animated filling battery: cycle EMPTY..FULL at ~400 ms steps.
        static const char *fill[] = {
            LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1, LV_SYMBOL_BATTERY_2,
            LV_SYMBOL_BATTERY_3, LV_SYMBOL_BATTERY_FULL,
        };
        int ph = (int)((esp_timer_get_time() / 1000 / 400) % 5);
        snprintf(b, sizeof(b), "%s %s CHARGING", LV_SYMBOL_CHARGE, fill[ph]);
        lv_label_set_text(s_chg_title, b);
    }
    snprintf(b, sizeof(b), "%d%%", soc);                       lv_label_set_text(s_chg_soc, b);
    snprintf(b, sizeof(b), "%.1f A   %.1f V", charge_a, pack_v); lv_label_set_text(s_chg_cur, b);
    lvgl_port_unlock();
}
