// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// range.c - charge-based (coulomb-counting) adaptive range estimate.
//
// While riding, integrate charge drawn from the pack (Ah += pack_A x dt)
// alongside distance (odometer x wheel circ). Over each distance window we get a
// consumption figure Ah/km, folded into an EMA so it tracks recent conditions.
// Remaining charge is SoC% x pack_capacity_Ah, and
//     range = remaining_Ah / Ah_per_km.
// Charge counting (not energy) is used because SoC is itself a charge metric, so
// this needs no pack-voltage term and the learned rate is stable across the
// discharge. Ah/km is seeded from capacity / range_full_km, so before anything
// is learned the estimate equals the old linear range_full_km x SoC%. The
// learned rate is clamped, outliers rejected, and it persists to NVS.

#include "range.h"
#include "config.h"

#include "esp_timer.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "range";

#define NVS_NAMESPACE "scooter"
#define NVS_KEY       "range_ahpk"   // uint32, milli-Ah per km

#define AHPK_MIN      0.05f          // Ah/km sanity floor
#define AHPK_MAX      3.0f           // Ah/km sanity ceiling
#define EMA_ALPHA     0.20f          // adapt over ~5 windows
#define LEARN_MIN_KM  0.2f           // close a learning window after this distance
#define DT_MAX_H      0.01f          // ignore integration steps longer than ~36 s

static float    s_ahpk;             // learned consumption, Ah/km
static double   s_ah;               // charge accumulated in the current window
static uint32_t s_mark_odo;         // odometer at the window start
static bool     s_have_mark;
static int64_t  s_last_us;          // for dt

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float capacity_ah(void)
{
    return g_cfg.pack_capacity_mah / 1000.0f;
}

static float seed_ahpk(void)
{
    float cap = capacity_ah();
    float rkm = g_cfg.range_full_km;
    if (cap > 0 && rkm > 0) return clampf(cap / rkm, AHPK_MIN, AHPK_MAX);
    return 0.4f;                     // generic scooter consumption
}

static void new_window(uint32_t odo)
{
    s_mark_odo  = odo;
    s_ah        = 0;
    s_have_mark = true;
}

void range_init(void)
{
    s_ahpk = seed_ahpk();

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t milli = 0;
        if (nvs_get_u32(h, NVS_KEY, &milli) == ESP_OK && milli > 0) {
            s_ahpk = clampf(milli / 1000.0f, AHPK_MIN, AHPK_MAX);
        }
        nvs_close(h);
    }
    s_have_mark = false;
    s_last_us = esp_timer_get_time();
    ESP_LOGI(TAG, "range init: %.3f Ah/km (cap %.1f Ah)", s_ahpk, capacity_ah());
}

void range_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY, (uint32_t)(s_ahpk * 1000.0f + 0.5f));
        nvs_commit(h);
        nvs_close(h);
    }
}

void range_reset(void)
{
    s_ahpk = seed_ahpk();
    s_have_mark = false;
    range_save();
}

void range_update(int soc, uint32_t odo, int cur_cA, bool moving, bool charging)
{
    int64_t now = esp_timer_get_time();
    double  dt_h = (double)(now - s_last_us) / 3.6e9;   // microseconds -> hours
    s_last_us = now;

    // Learn only from riding: not charging, wheel turning, valid SoC.
    if (charging || soc < 0 || !moving) { s_have_mark = false; return; }

    if (!s_have_mark)      { new_window(odo); return; }
    if (odo < s_mark_odo)  { new_window(odo); return; }   // odometer snapped back

    if (dt_h > 0 && dt_h < DT_MAX_H) {
        s_ah += (cur_cA / 100.0) * dt_h;                   // Ah this step (A x h)
    }

    double dist_km = (double)(odo - s_mark_odo) * g_cfg.wheel_circ_mm / 1.0e6;
    if (dist_km >= LEARN_MIN_KM) {
        float sample = (float)(s_ah / dist_km);            // Ah/km
        if (sample >= AHPK_MIN && sample <= AHPK_MAX) {    // reject outliers
            s_ahpk += EMA_ALPHA * (sample - s_ahpk);
        }
        new_window(odo);
    }
}

float range_km(int soc)
{
    if (soc < 0 || s_ahpk <= 0 || g_cfg.pack_capacity_mah == 0) return -1.0f;
    double remaining_ah = (soc / 100.0) * capacity_ah();
    return (float)(remaining_ah / s_ahpk);
}

float range_ah_per_km(void)
{
    return s_ahpk;
}
