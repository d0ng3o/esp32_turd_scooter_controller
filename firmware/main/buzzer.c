// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// buzzer.c - passive buzzer driven by LEDC PWM on PIN_BUZZER (low-side MOSFET).
// A dedicated task plays queued beep patterns so callers never block.

#include "buzzer.h"
#include "board.h"

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define LEDC_MODE     LEDC_LOW_SPEED_MODE
#define LEDC_TIMER    LEDC_TIMER_0
#define LEDC_CHANNEL  LEDC_CHANNEL_0
#define LEDC_RES      LEDC_TIMER_10_BIT     // duty 0..1023
#define DUTY_ON       512                   // ~50 %

typedef struct {
    uint16_t freq;
    uint16_t on_ms;
    uint16_t off_ms;
    uint8_t  count;
} beep_t;

static QueueHandle_t s_q;

static void tone_on(uint16_t freq)
{
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, DUTY_ON);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void tone_off(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    beep_t b;
    for (;;) {
        if (xQueueReceive(s_q, &b, portMAX_DELAY) == pdTRUE) {
            for (uint8_t i = 0; i < b.count; i++) {
                tone_on(b.freq);
                vTaskDelay(pdMS_TO_TICKS(b.on_ms));
                tone_off();
                if (b.off_ms) vTaskDelay(pdMS_TO_TICKS(b.off_ms));
            }
        }
    }
}

esp_err_t buzzer_init(void)
{
    const ledc_timer_config_t tcfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = BUZZER_TONE_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) return err;

    const ledc_channel_config_t ccfg = {
        .gpio_num   = PIN_BUZZER,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,                 // start silent
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) return err;

    s_q = xQueueCreate(8, sizeof(beep_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    return (xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 3, NULL) == pdPASS)
           ? ESP_OK : ESP_FAIL;
}

void buzzer_pattern(uint16_t freq, uint16_t on_ms, uint16_t off_ms, uint8_t count)
{
    beep_t b = { .freq = freq, .on_ms = on_ms, .off_ms = off_ms, .count = count };
    if (s_q) xQueueSend(s_q, &b, 0);     // drop if queue full (non-blocking)
}

void buzzer_boot(void)
{
    buzzer_pattern(BUZZER_TONE_HZ, 80, 0, 1);
}

void buzzer_fault(void)
{
    buzzer_pattern(2200, 120, 80, 3);
}
