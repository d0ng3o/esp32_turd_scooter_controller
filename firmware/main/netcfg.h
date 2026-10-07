// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// netcfg.h - WiFi SoftAP config portal + OTA (entered by the config gesture).

#pragma once

#include <stdbool.h>

// Start the SoftAP + HTTP config/OTA server (idempotent).
void netcfg_start(void);

// Stop the portal and the AP.
void netcfg_stop(void);

bool netcfg_is_active(void);

// Call periodically from the ride loop; auto-stops after config_timeout_s of no
// HTTP requests.
void netcfg_tick(void);
