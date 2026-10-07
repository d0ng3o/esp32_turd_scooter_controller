// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// i2cbus.h - the shared I2C master bus (D4/D5), used by the IMU and the OLED.

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// Create the shared bus (idempotent).
esp_err_t i2cbus_init(void);

// Handle for adding devices / panel IO. NULL if init failed.
i2c_master_bus_handle_t i2cbus_handle(void);
