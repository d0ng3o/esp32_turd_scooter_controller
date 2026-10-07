// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung

#include "config.h"
#include "board.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "cJSON.h"

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

char *config_to_json(void)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddNumberToObject(r, "thr_raw_min", g_cfg.thr_raw_min);
    cJSON_AddNumberToObject(r, "thr_raw_max", g_cfg.thr_raw_max);
    cJSON_AddNumberToObject(r, "brk_raw_min", g_cfg.brk_raw_min);
    cJSON_AddNumberToObject(r, "brk_raw_max", g_cfg.brk_raw_max);
    cJSON_AddNumberToObject(r, "thr_deadzone_pct", g_cfg.thr_deadzone_pct);
    cJSON_AddNumberToObject(r, "brk_active_pct", g_cfg.brk_active_pct);
    cJSON_AddNumberToObject(r, "throttle_cap", g_cfg.throttle_cap);
    cJSON_AddNumberToObject(r, "soft_start_ms", g_cfg.soft_start_ms);
    cJSON_AddNumberToObject(r, "throttle_curve", g_cfg.throttle_curve);
    cJSON_AddNumberToObject(r, "auto_lock_timeout_s", g_cfg.auto_lock_timeout_s);
    cJSON_AddBoolToObject(r, "motion_alarm", g_cfg.motion_alarm);
    cJSON_AddNumberToObject(r, "auto_sleep_timeout_s", g_cfg.auto_sleep_timeout_s);
    cJSON_AddNumberToObject(r, "headlight_mode", g_cfg.headlight_mode);
    cJSON_AddNumberToObject(r, "brakelight_mode", g_cfg.brakelight_mode);
    cJSON_AddNumberToObject(r, "headlight_off_delay_ms", g_cfg.headlight_off_delay_ms);
    cJSON_AddNumberToObject(r, "imu_wake_sens", g_cfg.imu_wake_sens);
    cJSON_AddBoolToObject(r, "buzzer_enable", g_cfg.buzzer_enable);
    cJSON_AddNumberToObject(r, "led_brightness", g_cfg.led_brightness);
    cJSON_AddStringToObject(r, "ap_ssid", g_cfg.ap_ssid);
    cJSON_AddStringToObject(r, "ap_pass", g_cfg.ap_pass);
    cJSON_AddNumberToObject(r, "config_timeout_s", g_cfg.config_timeout_s);
    cJSON_AddNumberToObject(r, "units", g_cfg.units);
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}

bool config_from_json(const char *json, int len)
{
    cJSON *r = cJSON_ParseWithLength(json, len);
    if (!r) return false;

#define GN(key, field, lo, hi) do { \
        cJSON *_i = cJSON_GetObjectItemCaseSensitive(r, key); \
        if (cJSON_IsNumber(_i)) { \
            int _v = _i->valueint; \
            g_cfg.field = _v < (lo) ? (lo) : (_v > (hi) ? (hi) : _v); \
        } \
    } while (0)
#define GB(key, field) do { \
        cJSON *_i = cJSON_GetObjectItemCaseSensitive(r, key); \
        if (cJSON_IsBool(_i)) g_cfg.field = cJSON_IsTrue(_i); \
    } while (0)
#define GS(key, field) do { \
        cJSON *_i = cJSON_GetObjectItemCaseSensitive(r, key); \
        if (cJSON_IsString(_i) && _i->valuestring) { \
            strncpy(g_cfg.field, _i->valuestring, sizeof(g_cfg.field) - 1); \
            g_cfg.field[sizeof(g_cfg.field) - 1] = 0; } \
    } while (0)

    GN("thr_raw_min", thr_raw_min, 0, 4095);
    GN("thr_raw_max", thr_raw_max, 0, 4095);
    GN("brk_raw_min", brk_raw_min, 0, 4095);
    GN("brk_raw_max", brk_raw_max, 0, 4095);
    GN("thr_deadzone_pct", thr_deadzone_pct, 0, 90);
    GN("brk_active_pct", brk_active_pct, 1, 99);
    GN("throttle_cap", throttle_cap, 0, 255);
    GN("soft_start_ms", soft_start_ms, 0, 5000);
    GN("throttle_curve", throttle_curve, 0, 1);
    GN("auto_lock_timeout_s", auto_lock_timeout_s, 0, 3600);
    GB("motion_alarm", motion_alarm);
    GN("auto_sleep_timeout_s", auto_sleep_timeout_s, 0, 7200);
    GN("headlight_mode", headlight_mode, 0, 3);
    GN("brakelight_mode", brakelight_mode, 0, 2);
    GN("headlight_off_delay_ms", headlight_off_delay_ms, 0, 5000);
    GN("imu_wake_sens", imu_wake_sens, 0, 63);
    GB("buzzer_enable", buzzer_enable);
    GN("led_brightness", led_brightness, 0, 255);
    GS("ap_ssid", ap_ssid);
    GS("ap_pass", ap_pass);
    GN("config_timeout_s", config_timeout_s, 30, 3600);
    GN("units", units, 0, 1);

#undef GN
#undef GB
#undef GS
    cJSON_Delete(r);
    return config_save() == ESP_OK;
}
