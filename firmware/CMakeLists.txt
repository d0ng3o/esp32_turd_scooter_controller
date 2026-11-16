# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware version embedded in the app image (esp_app_get_description()->version,
# shown in the web UI and console). Bump on release; a git tag would override it.
set(PROJECT_VER "0.9.0")

project(turd_scooter_fw)
