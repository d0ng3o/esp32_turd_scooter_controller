// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// imu.c - LSM6DS3TR-C accelerometer in low-power wake-on-motion mode.
//
// The chip's built-in activity engine asserts INT1 (open-drain, active-low) when
// acceleration exceeds WAKE_UP_THS. While awake we poll WAKE_UP_SRC for the same
// event (idle keep-alive + locked alarm); for deep sleep, INT1 -> GPIO wake.
// Register sequence follows the LSM6DS3TR-C datasheet / ST wake-up app note.
// Addr 0x6A (SA0 = GND). *** Register bits worth re-checking on first hardware. ***

#include "imu.h"
#include "board.h"
#include "config.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "imu";

#define LSM_ADDR          0x6A
#define REG_WHO_AM_I      0x0F
#define REG_CTRL1_XL      0x10
#define REG_CTRL3_C       0x12
#define REG_CTRL6_C       0x15
#define REG_WAKE_UP_SRC   0x1B
#define REG_TAP_CFG       0x58
#define REG_WAKE_UP_THS   0x5B
#define REG_WAKE_UP_DUR   0x5C
#define REG_MD1_CFG       0x5E
#define WHO_AM_I_VAL      0x6A
#define WU_IA_BIT         0x08   // WAKE_UP_SRC: wake-up event active

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_present;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, sizeof(b), 100);
}

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static void configure(void)
{
    // CTRL3_C: BDU | H_LACTIVE | PP_OD | IF_INC  (INT1 open-drain, active-low)
    wr(REG_CTRL3_C, 0x74);
    // CTRL6_C: XL_HM_MODE = 1  (accelerometer low-power mode)
    wr(REG_CTRL6_C, 0x10);
    // CTRL1_XL: ODR 52 Hz, +/-2 g
    wr(REG_CTRL1_XL, 0x30);
    // TAP_CFG: INTERRUPTS_ENABLE | SLOPE_FDS  (enable IRQs + slope/HP filter)
    wr(REG_TAP_CFG, 0x90);
    // WAKE_UP_DUR: no extra duration
    wr(REG_WAKE_UP_DUR, 0x00);
    // WAKE_UP_THS: 6-bit threshold, ~FS/64 per LSB (lower = more sensitive)
    wr(REG_WAKE_UP_THS, g_cfg.imu_wake_sens & 0x3F);
    // MD1_CFG: route wake-up to INT1 (INT1_WU)
    wr(REG_MD1_CFG, 0x20);
}

esp_err_t imu_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = -1,                        // auto-select a free port
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    bc.flags.enable_internal_pullup = true;    // R11/R12 do the real pull-up on-board
    esp_err_t err = i2c_new_master_bus(&bc, &s_bus);
    if (err != ESP_OK) return err;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LSM_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dc, &s_dev);
    if (err != ESP_OK) return err;

    uint8_t who = 0;
    if (rd(REG_WHO_AM_I, &who) == ESP_OK && who == WHO_AM_I_VAL) {
        s_present = true;
        configure();
        ESP_LOGI(TAG, "LSM6DS3TR-C up; wake-on-motion armed (ths=%u)",
                 g_cfg.imu_wake_sens & 0x3F);
    } else {
        s_present = false;
        ESP_LOGW(TAG, "IMU not found (WHO_AM_I=0x%02x, expected 0x6A)", who);
    }
    return ESP_OK;   // absence is non-fatal; features degrade gracefully
}

bool imu_present(void) { return s_present; }

bool imu_motion(void)
{
    if (!s_present) return false;
    uint8_t src = 0;
    if (rd(REG_WAKE_UP_SRC, &src) != ESP_OK) return false;
    return (src & WU_IA_BIT) != 0;
}

void imu_arm_wake(void)
{
    if (!s_present) return;
    configure();
    uint8_t src;
    rd(REG_WAKE_UP_SRC, &src);   // clear any pending event so INT1 idles high
}
