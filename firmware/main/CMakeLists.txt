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
        "imu.c"
        "i2cbus.c"
        "display.c"
        "power.c"
        "netcfg.c"
        "buzzer.c"
        "status_led.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver              # gpio, uart, ledc
        esp_driver_i2c      # i2c_master (IMU + OLED)
        esp_adc             # adc_oneshot (throttle/brake/pack)
        esp_lcd             # OLED panel IO
        nvs_flash           # config store
        esp_timer           # timestamps
        esp_hw_support      # esp_sleep (deep sleep)
        esp_wifi            # SoftAP
        esp_netif           # AP netif
        esp_event           # wifi event loop
        esp_http_server     # config/OTA web server
        app_update          # OTA (esp_ota_ops)
        json                # cJSON (config serialisation)
    EMBED_TXTFILES "web/index.html"
    # espressif/led_strip is pulled in automatically via idf_component.yml
)
