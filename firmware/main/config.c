// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung

#include "config.h"
#include "board.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "cfg";

#define NVS_NAMESPACE "scooter"
#define NVS_KEY       "cfg"

config_t g_cfg;

void config_set_defaults(config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic   = CONFIG_MAGIC;
    c->version = CONFIG_VERSION;

    // calibration (placeholders from board.h until bench calibration)
    c->thr_raw_min = THROTTLE_RAW_MIN;
    c->thr_raw_max = THROTTLE_RAW_MAX;
    c->brk_raw_min = BRAKE_RAW_MIN;
    c->brk_raw_max = BRAKE_RAW_MAX;
    c->thr_deadzone_pct = THROTTLE_DEADZONE_PCT;
    c->brk_active_pct   = BRAKE_ACTIVE_PCT;

    // ride feel
    c->throttle_cap   = THROTTLE_CMD_MAX;
    c->soft_start_ms  = 400;            // gentle ramp to avoid jerky starts
    c->throttle_curve = THROTTLE_LINEAR;
    c->kick_to_start  = true;

    // lock / security
    c->auto_lock_timeout_s = 120;
    c->motion_alarm        = true;

    // lights
    c->headlight_mode         = HEADLIGHT_AUTO;
    c->brakelight_mode        = BRAKELIGHT_STROBE_TAIL;   // user's choice (experimental)
    c->headlight_off_delay_ms = 500;

    // IMU
    c->imu_wake_sens        = 32;
    c->auto_sleep_timeout_s = 600;

    // feedback
    c->buzzer_enable  = true;
    c->led_brightness = 64;

    // WiFi / OTA (change SSID/pass from the UI before first real use)
    strncpy(c->ap_ssid, "turd-scooter", sizeof(c->ap_ssid) - 1);
    strncpy(c->ap_pass, "scooter-config", sizeof(c->ap_pass) - 1);
    c->config_timeout_s = 300;

    // system
    c->units = UNITS_KMH;
}

void config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        config_t tmp;
        size_t sz = sizeof(tmp);
        esp_err_t r = nvs_get_blob(h, NVS_KEY, &tmp, &sz);
        nvs_close(h);
        if (r == ESP_OK && sz == sizeof(tmp) &&
            tmp.magic == CONFIG_MAGIC && tmp.version == CONFIG_VERSION) {
            g_cfg = tmp;
            ESP_LOGI(TAG, "config loaded from NVS");
            return;
        }
    }

    config_set_defaults(&g_cfg);
    if (config_save() == ESP_OK) {
        ESP_LOGI(TAG, "wrote default config to NVS");
    } else {
        ESP_LOGW(TAG, "running on defaults (NVS save failed)");
    }
}

esp_err_t config_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY, &g_cfg, sizeof(g_cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
