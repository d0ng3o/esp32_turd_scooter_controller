// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// imu.h - LSM6DS3TR-C (I2C) wake-on-motion sensing.

#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Init I2C, verify the chip, and configure the accelerometer in low-power mode
// with the wake-on-motion engine routed to INT1 (open-drain, active-low). Safe
// to call even if the IMU is absent (returns ESP_OK; imu_present() stays false).
esp_err_t imu_init(void);

// True if the WHO_AM_I check passed.
bool imu_present(void);

// True if a motion (wake-up) event is currently flagged. Used both for the
// idle keep-alive and the locked motion alarm.
bool imu_motion(void);

// Re-arm the wake engine and clear any pending event (call right before entering
// deep sleep so the INT line starts idle-high).
void imu_arm_wake(void);
