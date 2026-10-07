# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
idf_component_register(
    SRCS
        "app_main.c"
        "config.c"
        "protocol.c"
        "bus.c"
        "inputs.c"
        "control.c"
        "gesture.c"
        "security.c"
        "buzzer.c"
        "status_led.c"
    INCLUDE_DIRS "."
)
