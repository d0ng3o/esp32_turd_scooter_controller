// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// inputs.c - throttle/brake ADC sampling, light filtering, and mapping to the
// 0..255 command byte. Calibration endpoints are compile-time placeholders
// (board.h); a runtime calibration + NVS store is a later phase.

#include "inputs.h"
#include "board.h"
#include "config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "inputs";

#define OVERSAMPLE 8            // averaged reads per channel per sample
#define EMA_SHIFT  2           // exponential smoothing: new = old + (x-old)>>SHIFT

static adc_oneshot_unit_handle_t s_adc1;
static adc_oneshot_unit_handle_t s_adc2;      // pack sense (GPIO5)
static adc_cali_handle_t         s_pack_cali; // NULL -> fall back to rough scaling
static bool     s_pack_ready;
static uint32_t s_pack_mv;
static int s_thr_ema = -1;     // -1 = uninitialised
static int s_brk_ema = -1;
static int s_last_thr_raw, s_last_brk_raw;

// Guided calibration capture.
static bool s_cal_active;
static int  s_cal_tmin = 4095, s_cal_tmax, s_cal_bmin = 4095, s_cal_bmax;

static int read_avg(adc_channel_t ch)
{
    int acc = 0, got = 0;
    for (int i = 0; i < OVERSAMPLE; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc1, ch, &raw) == ESP_OK) {
            acc += raw;
            got++;
        }
    }
    return got ? acc / got : 0;
}

static int ema(int *state, int x)
{
    if (*state < 0) *state = x;
    else *state += (x - *state) >> EMA_SHIFT;
    return *state;
}

// Map raw ADC to 0..255 over [rawmin, rawmax], with a low deadzone.
static uint8_t map_cmd(int raw, int rawmin, int rawmax, int deadzone_pct)
{
    if (rawmax <= rawmin) return 0;
    int span = rawmax - rawmin;
    int pct = (raw - rawmin) * 100 / span;           // 0..100 (may over/undershoot)
    if (pct <= deadzone_pct) return 0;
    if (pct >= 100) return THROTTLE_CMD_MAX;
    // rescale above the deadzone so travel still reaches full
    int eff = (pct - deadzone_pct) * 100 / (100 - deadzone_pct);
    int cmd = eff * THROTTLE_CMD_MAX / 100;
    return (uint8_t)(cmd > THROTTLE_CMD_MAX ? THROTTLE_CMD_MAX : cmd);
}

esp_err_t inputs_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc1);
    if (err != ESP_OK) return err;

    const adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,     // full ~0..3.1 V range
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, ADC_THROTTLE_CHANNEL, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1, ADC_BRAKE_CHANNEL, &ch_cfg));

    ESP_LOGI(TAG, "ADC1 up: throttle=CH%d(GPIO%d) brake=CH%d(GPIO%d)",
             ADC_THROTTLE_CHANNEL, PIN_THROTTLE_ADC, ADC_BRAKE_CHANNEL, PIN_BRAKE_ADC);

    // ADC2 for pack sense (unavailable while WiFi runs; read is gated by caller).
    const adc_oneshot_unit_init_cfg_t unit2_cfg = { .unit_id = ADC_UNIT_2 };
    if (adc_oneshot_new_unit(&unit2_cfg, &s_adc2) == ESP_OK &&
        adc_oneshot_config_channel(s_adc2, ADC_PACK_CHANNEL, &ch_cfg) == ESP_OK) {
        s_pack_ready = true;
        const adc_cali_curve_fitting_config_t cali = {
            .unit_id  = ADC_UNIT_2,
            .chan     = ADC_PACK_CHANNEL,
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali, &s_pack_cali) != ESP_OK) {
            s_pack_cali = NULL;   // rough scaling fallback
        }
        ESP_LOGI(TAG, "ADC2 up: pack=CH%d(GPIO%d)%s", ADC_PACK_CHANNEL, PIN_PACK_ADC,
                 s_pack_cali ? " (calibrated)" : "");
    } else {
        ESP_LOGW(TAG, "ADC2 (pack) init failed");
    }
    return ESP_OK;
}

void inputs_sample_pack(bool allowed)
{
    if (!allowed || !s_pack_ready) return;
    int raw = 0;
    if (adc_oneshot_read(s_adc2, ADC_PACK_CHANNEL, &raw) != ESP_OK) return;

    int node_mv = 0;
    if (s_pack_cali) {
        adc_cali_raw_to_voltage(s_pack_cali, raw, &node_mv);
    } else {
        node_mv = raw * 3100 / 4095;   // rough (12 dB full-scale ~3.1 V)
    }
    s_pack_mv = (uint32_t)node_mv * (PACK_DIV_R_TOP + PACK_DIV_R_BOT) / PACK_DIV_R_BOT;
}

uint32_t inputs_pack_mv(void)
{
    return s_pack_mv;
}

int inputs_thr_raw(void) { return s_last_thr_raw; }
int inputs_brk_raw(void) { return s_last_brk_raw; }

void inputs_cal_start(void)
{
    s_cal_tmin = 4095; s_cal_tmax = 0;
    s_cal_bmin = 4095; s_cal_bmax = 0;
    s_cal_active = true;
}

void inputs_cal_stop(void) { s_cal_active = false; }
bool inputs_cal_active(void) { return s_cal_active; }

void inputs_cal_values(int *tmin, int *tmax, int *bmin, int *bmax)
{
    *tmin = s_cal_tmin; *tmax = s_cal_tmax;
    *bmin = s_cal_bmin; *bmax = s_cal_bmax;
}

void inputs_sample(inputs_t *out)
{
    int thr = ema(&s_thr_ema, read_avg(ADC_THROTTLE_CHANNEL));
    int brk = ema(&s_brk_ema, read_avg(ADC_BRAKE_CHANNEL));

    out->throttle_raw  = thr;
    out->brake_raw     = brk;
    s_last_thr_raw = thr;
    s_last_brk_raw = brk;

    if (s_cal_active) {
        if (thr < s_cal_tmin) s_cal_tmin = thr;
        if (thr > s_cal_tmax) s_cal_tmax = thr;
        if (brk < s_cal_bmin) s_cal_bmin = brk;
        if (brk > s_cal_bmax) s_cal_bmax = brk;
    }

    // Full-scale 0..255 from the calibrated travel; ride-feel shaping (cap,
    // curve, soft-start) happens in control.c.
    out->throttle_cmd  = map_cmd(thr, g_cfg.thr_raw_min, g_cfg.thr_raw_max,
                                 g_cfg.thr_deadzone_pct);

    int brk_pct = (g_cfg.brk_raw_max > g_cfg.brk_raw_min)
                  ? (brk - g_cfg.brk_raw_min) * 100 / (g_cfg.brk_raw_max - g_cfg.brk_raw_min)
                  : 0;
    out->brake_active = brk_pct >= g_cfg.brk_active_pct;
}
