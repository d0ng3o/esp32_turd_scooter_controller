// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// sercon.h - USB-serial console (esp_console REPL) mirroring the web config.

#pragma once

// Start the interactive console on the USB-Serial-JTAG port.
void sercon_init(void);
