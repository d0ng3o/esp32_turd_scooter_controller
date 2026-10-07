// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// board.h - one place for the XIAO ESP32-C3 pin map and firmware tunables.
// Silk labels (D0..D10) are the XIAO's; GPIOs are the ESP32-C3's.

#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "driver/uart.h"

// ---- Pin map (XIAO silk -> GPIO) --------------------------------------------
#define PIN_IMU_INT       GPIO_NUM_2    // D0  IMU INT1 / deep-sleep wake (later)
#define PIN_BRAKE_ADC     GPIO_NUM_3    // D1  brake input     (ADC1_CH3)
#define PIN_THROTTLE_ADC  GPIO_NUM_4    // D2  throttle input  (ADC1_CH4)
#define PIN_PACK_ADC      GPIO_NUM_5    // D3  pack sense      (ADC2_CH0, later)
#define PIN_I2C_SDA       GPIO_NUM_6    // D4  OLED + IMU       (later)
#define PIN_I2C_SCL       GPIO_NUM_7    // D5  OLED + IMU       (later)
#define PIN_BUS_TX        GPIO_NUM_21   // D6  one-wire bus TX (open-drain)
#define PIN_BUS_RX        GPIO_NUM_20   // D7  one-wire bus RX (tied to D6)
#define PIN_WS2812        GPIO_NUM_8    // D8  status LED data
#define PIN_BUZZER        GPIO_NUM_9    // D9  buzzer (low-side MOSFET gate)
#define PIN_EXPANSION     GPIO_NUM_10   // D10 expansion header (unused in fw)

// ---- ADC channels (ESP32-C3) ------------------------------------------------
#define ADC_BRAKE_CHANNEL     ADC_CHANNEL_3   // GPIO3
#define ADC_THROTTLE_CHANNEL  ADC_CHANNEL_4   // GPIO4

// ---- One-wire bus -----------------------------------------------------------
#define BUS_UART_NUM      UART_NUM_1
#define BUS_BAUD          115200
#define POLL_PERIOD_MS    20             // 50 Hz heartbeat (PROTOCOL.md 3.1/5.2)
#define STATUS_EVERY_N    5              // request status on every 5th poll
#define BUS_LINK_TIMEOUT_US  300000      // no status for 300 ms => link lost

// ---- Buzzer -----------------------------------------------------------------
#define BUZZER_TONE_HZ    2700           // HYG-8503A is loudest near 2.7 kHz

// ---- Throttle / brake input (RAW 12-bit ADC endpoints) ----------------------
// Placeholder calibration until the real harness is on the bench. The dividers
// (R1-R4, 100k) halve the hall signal; these get replaced by a runtime
// calibration (serial-triggered) and NVS storage in a later phase.
#define THROTTLE_RAW_MIN  600            // released
#define THROTTLE_RAW_MAX  2600           // full
#define BRAKE_RAW_MIN     600            // released
#define BRAKE_RAW_MAX     2600           // full
#define THROTTLE_DEADZONE_PCT  5         // ignore the bottom 5 % of travel
#define BRAKE_ACTIVE_PCT       40        // brake "on" past ~40 % travel (PROTOCOL.md 6.3)

// ---- Control ----------------------------------------------------------------
#define THROTTLE_CMD_MAX  255            // cap sent to controller (0..255)

// ---- Gestures (percent of throttle travel; evaluated only while stationary) --
// Unlock/lock toggle: hold brake + blip the throttle GEST_TRIPLE_COUNT times.
#define GEST_BLIP_HIGH_PCT     60        // throttle must exceed this to start a blip
#define GEST_BLIP_LOW_PCT      10        // ...then drop below this to complete it
#define GEST_TRIPLE_COUNT       3
#define GEST_TRIPLE_WINDOW_MS  3000      // all blips must land within this window
// Config/OTA mode: hold brake + full throttle continuously for this long.
#define GEST_CONFIG_HIGH_PCT   90
#define GEST_CONFIG_HOLD_MS   20000      // 20 s (deliberately awkward)
