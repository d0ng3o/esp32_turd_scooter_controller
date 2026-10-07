// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// inputs.c - throttle/brake ADC sampling, light filtering, and mapping to the
// 0..255 command byte. Calibration endpoints are compile-time placeholders
// (board.h); a runtime calibration + NVS store is a later phase.

#include "inputs.h"
#include "board.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "inputs";

#define OVERSAMPLE 8            // averaged reads per channel per sample
#define EMA_SHIFT  2           // exponential smoothing: new = old + (x-old)>>SHIFT

static adc_oneshot_unit_handle_t s_adc1;
static int s_thr_ema = -1;     // -1 = uninitialised
static int s_brk_ema = -1;

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
    return ESP_OK;
}

void inputs_sample(inputs_t *out)
{
    int thr = ema(&s_thr_ema, read_avg(ADC_THROTTLE_CHANNEL));
    int brk = ema(&s_brk_ema, read_avg(ADC_BRAKE_CHANNEL));

    out->throttle_raw  = thr;
    out->brake_raw     = brk;
    out->throttle_cmd  = map_cmd(thr, THROTTLE_RAW_MIN, THROTTLE_RAW_MAX, THROTTLE_DEADZONE_PCT);

    int brk_pct = (BRAKE_RAW_MAX > BRAKE_RAW_MIN)
                  ? (brk - BRAKE_RAW_MIN) * 100 / (BRAKE_RAW_MAX - BRAKE_RAW_MIN)
                  : 0;
    out->brake_active = brk_pct >= BRAKE_ACTIVE_PCT;
}
