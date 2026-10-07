// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// netcfg.c - config/OTA mode: WiFi SoftAP + HTTP server.
//
// Entered by the brake+full-throttle-20s gesture. Serves a single-page config UI
// (embedded index.html) with a JSON config API and an OTA firmware upload. The
// AP is at 192.168.4.1. Throttle stays inhibited while this is active (app_main),
// and it auto-exits after config_timeout_s of inactivity.

#include "netcfg.h"
#include "config.h"
#include "bus.h"
#include "inputs.h"
#include "security.h"
#include "buzzer.h"

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

static httpd_handle_t s_server;
static bool    s_wifi_inited;
static bool    s_active;
static int64_t s_last_req_us;

static void touch(void) { s_last_req_us = esp_timer_get_time(); }

// ---- HTTP handlers ----------------------------------------------------------

static esp_err_t h_root(httpd_req_t *req)
{
    touch();
    httpd_resp_set_type(req, "text/html");
    const size_t len = (index_html_end - index_html_start) - 1;   // drop embed NUL
    return httpd_resp_send(req, index_html_start, len);
}

static esp_err_t h_cfg_get(httpd_req_t *req)
{
    touch();
    char *json = config_to_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "{}");
    free(json);
    return ESP_OK;
}

static esp_err_t h_cfg_post(httpd_req_t *req)
{
    touch();
    int len = req->content_len;
    if (len <= 0 || len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char *buf = malloc(len + 1);
    if (!buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int got = 0;
    while (got < len) {
        int k = httpd_req_recv(req, buf + got, len - got);
        if (k <= 0) { free(buf); return ESP_FAIL; }
        got += k;
    }
    buf[len] = 0;
    bool ok = config_from_json(buf, len);
    free(buf);
    if (ok) { httpd_resp_sendstr(req, "{\"ok\":true}"); }
    else    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse/save failed"); }
    return ESP_OK;
}

static esp_err_t h_status(httpd_req_t *req)
{
    touch();
    telemetry_t t;
    bus_get_telemetry(&t);
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"link\":%d,\"locked\":%d,\"speed\":%u,\"soc\":%u,"
             "\"current_cA\":%u,\"temp1\":%u,\"odo\":%lu,\"pack_mv\":%lu}",
             bus_link_ok() ? 1 : 0, security_is_locked() ? 1 : 0,
             t.speed, t.soc, t.current_cA, t.temp1, (unsigned long)t.odometer,
             (unsigned long)inputs_pack_mv());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t h_ota(httpd_req_t *req)
{
    touch();
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA slot"); return ESP_FAIL; }

    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota begin");
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "OTA -> partition '%s'", part->label);

    char buf[1024];
    int remaining = req->content_len;
    esp_err_t err = ESP_OK;
    while (remaining > 0) {
        int want = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int k = httpd_req_recv(req, buf, want);
        if (k == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (k <= 0) { err = ESP_FAIL; break; }
        if (esp_ota_write(ota, buf, k) != ESP_OK) { err = ESP_FAIL; break; }
        remaining -= k;
    }

    if (err == ESP_OK && esp_ota_end(ota) == ESP_OK &&
        esp_ota_set_boot_partition(part) == ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":true}");
        ESP_LOGW(TAG, "OTA complete, rebooting");
        vTaskDelay(pdMS_TO_TICKS(600));
        esp_restart();
    } else {
        esp_ota_abort(ota);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
    }
    return ESP_OK;
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

// ---- WiFi AP + server -------------------------------------------------------

static void wifi_init_once(void)
{
    if (s_wifi_inited) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    s_wifi_inited = true;
}

static void wifi_start_ap(void)
{
    wifi_config_t wc = { 0 };
    size_t sl = strnlen(g_cfg.ap_ssid, sizeof(wc.ap.ssid));
    if (sl == 0) { strcpy((char *)wc.ap.ssid, "turd-scooter"); sl = strlen("turd-scooter"); }
    else         { memcpy(wc.ap.ssid, g_cfg.ap_ssid, sl); }
    wc.ap.ssid_len = sl;
    wc.ap.channel = 1;
    wc.ap.max_connection = 4;
    wc.ap.beacon_interval = 100;

    size_t pl = strnlen(g_cfg.ap_pass, sizeof(wc.ap.password));
    if (pl >= 8) {
        memcpy(wc.ap.password, g_cfg.ap_pass, pl);
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;   // too-short password -> open AP
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void register_handlers(void)
{
    const httpd_uri_t uris[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = h_root },
        { .uri = "/api/config",  .method = HTTP_GET,  .handler = h_cfg_get },
        { .uri = "/api/config",  .method = HTTP_POST, .handler = h_cfg_post },
        { .uri = "/api/status",  .method = HTTP_GET,  .handler = h_status },
        { .uri = "/api/ota",     .method = HTTP_POST, .handler = h_ota },
        { .uri = "/api/reboot",  .method = HTTP_POST, .handler = h_reboot },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
}

void netcfg_start(void)
{
    if (s_active) return;
    ESP_LOGW(TAG, "config mode ON - AP '%s' @ 192.168.4.1", g_cfg.ap_ssid);

    wifi_init_once();
    wifi_start_ap();

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.lru_purge_enable = true;
    hc.max_uri_handlers = 8;
    hc.stack_size = 8192;
    if (httpd_start(&s_server, &hc) == ESP_OK) {
        register_handlers();
    } else {
        ESP_LOGE(TAG, "httpd_start failed");
    }

    s_active = true;
    touch();
    buzzer_pattern(2000, 80, 60, 2);
}

void netcfg_stop(void)
{
    if (!s_active) return;
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
    esp_wifi_stop();
    s_active = false;
    ESP_LOGI(TAG, "config mode OFF");
    buzzer_pattern(1800, 120, 0, 1);
}

bool netcfg_is_active(void) { return s_active; }

void netcfg_tick(void)
{
    if (!s_active) return;
    uint32_t to = g_cfg.config_timeout_s ? g_cfg.config_timeout_s : 300;
    if (esp_timer_get_time() - s_last_req_us > (int64_t)to * 1000000) {
        netcfg_stop();
    }
}
