// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung

#include "i2cbus.h"
#include "board.h"

static i2c_master_bus_handle_t s_bus;
static bool s_inited;

esp_err_t i2cbus_init(void)
{
    if (s_inited) return ESP_OK;
    i2c_master_bus_config_t bc = {
        .i2c_port = -1,                        // auto-select a free port
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    bc.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&bc, &s_bus);
    if (err == ESP_OK) s_inited = true;
    return err;
}

i2c_master_bus_handle_t i2cbus_handle(void) { return s_bus; }
