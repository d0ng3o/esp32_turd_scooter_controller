// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// sercon.c - interactive USB-serial console. Mirrors the web UI by reusing the
// same config/calibration backend (config_from_json, inputs_cal_*). Commands:
//   get | set <key> <value> | status | cal start|apply|cancel|show |
//   wifi on|off | defaults | reboot
// Type `help` for the list. Runs on the XIAO's native USB-Serial-JTAG.

#include "sercon.h"
#include "config.h"
#include "inputs.h"
#include "bus.h"
#include "security.h"
#include "netcfg.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_console.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int cmd_get(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *j = config_to_json();
    if (j) { printf("%s\n", j); free(j); }
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc < 3) { printf("usage: set <key> <value>\n"); return 1; }
    char json[160], *end;
    (void)strtol(argv[2], &end, 10);
    bool is_num  = (argv[2][0] != 0 && *end == 0);
    bool is_bool = (strcmp(argv[2], "true") == 0 || strcmp(argv[2], "false") == 0);
    if (is_num || is_bool) snprintf(json, sizeof(json), "{\"%s\":%s}", argv[1], argv[2]);
    else                   snprintf(json, sizeof(json), "{\"%s\":\"%s\"}", argv[1], argv[2]);
    bool ok = config_from_json(json, strlen(json));
    printf("%s\n", ok ? "ok" : "error (bad key/value?)");
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    telemetry_t t;
    bus_get_telemetry(&t);
    printf("link=%d locked=%d speed=%u soc=%u%% current=%.2fA pack=%.1fV temp1=%u odo=%lu\n",
           bus_link_ok(), security_is_locked(), t.speed, t.soc,
           t.current_cA / 100.0, inputs_pack_mv() / 1000.0, t.temp1,
           (unsigned long)t.odometer);
    return 0;
}

static int cmd_cal(int argc, char **argv)
{
    const char *a = (argc > 1) ? argv[1] : "show";
    if (!strcmp(a, "start")) {
        inputs_cal_start();
        printf("calibrating: sweep throttle fully and squeeze the brake, then `cal apply`\n");
    } else if (!strcmp(a, "apply")) {
        int tmin, tmax, bmin, bmax;
        inputs_cal_values(&tmin, &tmax, &bmin, &bmax);
        if (tmax > tmin + 100 && bmax > bmin + 100) {
            char j[160];
            snprintf(j, sizeof(j),
                     "{\"thr_raw_min\":%d,\"thr_raw_max\":%d,\"brk_raw_min\":%d,\"brk_raw_max\":%d}",
                     tmin, tmax, bmin, bmax);
            config_from_json(j, strlen(j));
            inputs_cal_stop();
            printf("applied: throttle[%d..%d] brake[%d..%d]\n", tmin, tmax, bmin, bmax);
        } else {
            printf("not enough travel captured - move the controls through full range first\n");
        }
    } else if (!strcmp(a, "cancel")) {
        inputs_cal_stop();
        printf("cancelled\n");
    } else {
        int tmin, tmax, bmin, bmax;
        inputs_cal_values(&tmin, &tmax, &bmin, &bmax);
        printf("active=%d throttle=%d[%d..%d] brake=%d[%d..%d]\n",
               inputs_cal_active(), inputs_thr_raw(), tmin, tmax,
               inputs_brk_raw(), bmin, bmax);
    }
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "on"))  { netcfg_start(); printf("config AP up @ 192.168.4.1\n"); }
    else if (argc > 1 && !strcmp(argv[1], "off")) { netcfg_stop(); printf("config AP down\n"); }
    else printf("usage: wifi on|off  (active=%d)\n", netcfg_is_active());
    return 0;
}

static int cmd_defaults(int argc, char **argv)
{
    (void)argc; (void)argv;
    config_set_defaults(&g_cfg);
    config_save();
    printf("config reset to defaults (saved)\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("rebooting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return 0;
}

void sercon_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "scooter>";
    rc.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t dc = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dc, &rc, &repl));

    const esp_console_cmd_t cmds[] = {
        { .command = "get",      .help = "print the current config (JSON)",      .func = cmd_get },
        { .command = "set",      .help = "set <key> <value> and save to NVS",     .func = cmd_set },
        { .command = "status",   .help = "live telemetry (link/lock/speed/...)",  .func = cmd_status },
        { .command = "cal",      .help = "cal start|apply|cancel|show",           .func = cmd_cal },
        { .command = "wifi",     .help = "wifi on|off  (config/OTA portal)",      .func = cmd_wifi },
        { .command = "defaults", .help = "reset config to defaults",              .func = cmd_defaults },
        { .command = "reboot",   .help = "restart the device",                    .func = cmd_reboot },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    esp_console_register_help_command();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
