// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// bus.c - one-wire open-drain UART driver + colon-protocol framer.
//
// The physical bus is a single wire (D6 TX and D7 RX are tied together on the
// board, R6 to the harness). TX is open-drain; the controller provides the
// pull-up. Because it is one wire, *every byte we transmit echoes back on RX*,
// so we strip our own transmitted bytes in software before parsing - the same
// technique used in scooter_master.py / pi_sniff.py.

#include "bus.h"
#include "board.h"

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "hal/uart_ll.h"
#include "soc/uart_periph.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bus";

#define RX_BUF_SIZE   512
#define TX_BUF_SIZE   256

static SemaphoreHandle_t s_lock;
static telemetry_t       s_telem;              // protected by s_lock
static int64_t           s_last_status_us = INT64_MIN / 2;

// Bring-up wire counters (see bus.h). Updated from the bus task without a lock
// (benign races - they're diagnostic); snapshotted under s_lock in bus_get_debug.
static bus_debug_t s_dbg;
static int         s_last_pos;                 // ring write index into s_dbg.last
static int8_t      s_tie_ok = -1;              // survives bus_reset_debug()
static int8_t      s_echo_nat = -1, s_echo_od = -1, s_echo_lb = -1;

static void dbg_push_foreign(uint8_t b)
{
    s_dbg.foreign_total++;
    if (b == FRAME_SOF) s_dbg.sof_seen++;
    s_dbg.last[s_last_pos] = b;
    s_last_pos = (s_last_pos + 1) & 15;
    if (s_dbg.last_len < 16) s_dbg.last_len++;
}

// ---- echo-expect ring: bytes we transmitted, awaiting their echo ------------
#define EXP_SIZE 256                            // power of two
static uint8_t s_exp[EXP_SIZE];
static int     s_exp_head, s_exp_tail, s_exp_count;

static void exp_push(const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (s_exp_count < EXP_SIZE) {
            s_exp[s_exp_tail] = b[i];
            s_exp_tail = (s_exp_tail + 1) & (EXP_SIZE - 1);
            s_exp_count++;
        } // else: overflow (shouldn't happen at 50 Hz) -> treat extra as foreign
    }
}

// If b matches the oldest expected echo byte, consume it and return true.
static bool exp_match_pop(uint8_t b)
{
    if (s_exp_count > 0 && s_exp[s_exp_head] == b) {
        s_exp_head = (s_exp_head + 1) & (EXP_SIZE - 1);
        s_exp_count--;
        return true;
    }
    return false;
}

// ---- foreign-byte parse buffer ----------------------------------------------
static uint8_t s_pbuf[256];
static int     s_plen;

static void parse_buffer(void)
{
    int i = 0;
    while (i < s_plen) {
        if (s_pbuf[i] != FRAME_SOF) { i++; continue; }
        if (s_plen - i < 3) break;                     // need TYPE + LEN
        uint8_t len = s_pbuf[i + 2];
        if (len > 64) { i++; continue; }               // implausible -> resync
        int end = i + 3 + len + 3;                      // ... CHK CR LF
        if (end > s_plen) break;                        // wait for more bytes
        uint8_t type = s_pbuf[i + 1];
        uint8_t chk  = xor_chk(type, &s_pbuf[i + 3], len);
        if (s_pbuf[i + 3 + len] == chk &&
            s_pbuf[end - 2] == FRAME_CR && s_pbuf[end - 1] == FRAME_LF) {
            if (type == MSG_STATUS && len == STATUS_LEN) {
                telemetry_t t;
                protocol_decode_status(&s_pbuf[i + 3], &t);
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_telem = t;
                s_last_status_us = t.t_us;
                xSemaphoreGive(s_lock);
                s_dbg.frames_ok++;
            }
            i = end;                                     // consumed a frame
        } else {
            i++;                                         // bad frame -> skip SOF
        }
    }
    if (i > 0) {                                         // drop consumed/leading junk
        memmove(s_pbuf, s_pbuf + i, s_plen - i);
        s_plen -= i;
    }
    if (s_plen == (int)sizeof(s_pbuf)) {                 // never wedge on a noisy line
        memmove(s_pbuf, s_pbuf + 64, s_plen - 64);
        s_plen -= 64;
    }
}

static void bus_process(const uint8_t *rx, int n)
{
    s_dbg.rx_total += n;
    for (int i = 0; i < n; i++) {
        if (exp_match_pop(rx[i])) { s_dbg.echo_total++; continue; }  // our own echo
        dbg_push_foreign(rx[i]);
        if (s_plen < (int)sizeof(s_pbuf)) {
            s_pbuf[s_plen++] = rx[i];                    // foreign byte -> parse
        }
    }
    parse_buffer();
}

static void bus_send(const uint8_t *frame, size_t len)
{
    uart_write_bytes(BUS_UART_NUM, frame, len);
    exp_push(frame, len);
    s_dbg.tx_total += len;
}

bool bus_selftest_tie(void)
{
    const gpio_config_t tx = {
        .pin_bit_mask = 1ULL << PIN_BUS_TX, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t rx = {
        .pin_bit_mask = 1ULL << PIN_BUS_RX, .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0, .pull_down_en = 1, .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tx);
    gpio_config(&rx);

    gpio_set_level(PIN_BUS_TX, 1);
    esp_rom_delay_us(100);
    int hi = gpio_get_level(PIN_BUS_RX);     // tied -> 1, open -> 0 (pulldown wins)
    gpio_set_level(PIN_BUS_TX, 0);
    esp_rom_delay_us(100);
    int lo = gpio_get_level(PIN_BUS_RX);     // tied -> 0

    bool ok = (hi == 1 && lo == 0);
    s_tie_ok = ok ? 1 : 0;
    ESP_LOGW(TAG, "D6<->D7 tie self-test: %s (drive-high read=%d, drive-low read=%d)",
             ok ? "OK - wire intact" : "OPEN - no continuity (check D6/D7 solder)", hi, lo);
    return ok;
}

// Send one byte and count the bytes that loop back on RX within a short window,
// using the pins exactly as currently configured. Used by the boot echo probes.
static int bus_probe_raw(void)
{
    uart_flush_input(BUS_UART_NUM);
    const uint8_t tb = 0x55;                 // 0b01010101 -> lots of edges
    uart_write_bytes(BUS_UART_NUM, &tb, 1);
    uart_wait_tx_done(BUS_UART_NUM, pdMS_TO_TICKS(20));

    uint8_t rx[16];
    int n = uart_read_bytes(BUS_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(20));
    return n < 0 ? 0 : n;
}

esp_err_t bus_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    const uart_config_t cfg = {
        .baud_rate  = BUS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BUS_UART_NUM, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BUS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BUS_UART_NUM, PIN_BUS_TX, PIN_BUS_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(gpio_set_pull_mode(PIN_BUS_TX, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(PIN_BUS_RX, GPIO_PULLUP_ONLY));

    // Probe 1 (native): exactly as uart_set_pin leaves TX - push-pull, UART matrix
    // out intact. Confirms the bare pin routing echoes over the D6<->D7 tie before
    // we touch the direction.
    s_echo_nat = bus_probe_raw();

    // Make the TX pad open-drain (so on the shared bus it only ever pulls low).
    // *** The fix: gpio_set_direction() re-points the pad OUTPUT to the GPIO
    // register, detaching the UART1 TX signal - which silently killed the bus.
    // Re-attach UART1 TX to the pad through the GPIO matrix afterwards. ***
    gpio_set_direction(PIN_BUS_TX, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(PIN_BUS_TX, GPIO_PULLUP_ONLY);
    esp_rom_gpio_connect_out_signal(PIN_BUS_TX,
        UART_PERIPH_SIGNAL(BUS_UART_NUM, SOC_UART_TX_PIN_IDX), false, false);
    s_echo_od = bus_probe_raw();

    // Probe 3 (internal loopback): UART core only, pads bypassed - sanity baseline.
    uart_ll_set_loop_back(UART_LL_GET_HW(BUS_UART_NUM), true);
    s_echo_lb = bus_probe_raw();
    uart_ll_set_loop_back(UART_LL_GET_HW(BUS_UART_NUM), false);

    ESP_LOGW(TAG, "bus echo probe: native=%d open-drain=%d loopback=%d (of 1 byte)",
             s_echo_nat, s_echo_od, s_echo_lb);

    ESP_LOGI(TAG, "one-wire bus up: UART%d TX=GPIO%d RX=GPIO%d @ %d 8N1 (open-drain)",
             BUS_UART_NUM, PIN_BUS_TX, PIN_BUS_RX, BUS_BAUD);
    return ESP_OK;
}

void bus_poll_once(uint8_t throttle, bool headlight, bool tail_on, bool want_status)
{
    uint8_t flags = 0;
    if (tail_on)     flags |= FLAG_TAILLIGHT;
    if (want_status) flags |= FLAG_STATUSREQ;
    if (headlight)   flags |= FLAG_HEADLIGHT;

    uint8_t frame[POLL_FRAME_LEN];
    size_t  flen = protocol_build_poll(frame, throttle, flags);
    bus_send(frame, flen);

    // Pump RX for the rest of the poll window (this is what paces the 50 Hz loop).
    uint8_t rx[128];
    int64_t deadline = esp_timer_get_time() + (int64_t)POLL_PERIOD_MS * 1000;
    while (esp_timer_get_time() < deadline) {
        int n = uart_read_bytes(BUS_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(2));
        if (n > 0) bus_process(rx, n);
    }
}

bool bus_get_telemetry(telemetry_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_telem;
    xSemaphoreGive(s_lock);
    return out->valid;
}

bool bus_link_ok(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t last = s_last_status_us;
    xSemaphoreGive(s_lock);
    return (esp_timer_get_time() - last) < BUS_LINK_TIMEOUT_US;
}

void bus_get_debug(bus_debug_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_dbg;
    // Linearise the last[] ring into oldest..newest order for display.
    int len = s_dbg.last_len;
    int start = (len < 16) ? 0 : s_last_pos;       // full -> oldest is at write head
    for (int i = 0; i < len; i++) out->last[i] = s_dbg.last[(start + i) & 15];
    out->tie_ok = s_tie_ok;
    out->echo_nat = s_echo_nat;
    out->echo_od = s_echo_od;
    out->echo_lb = s_echo_lb;
    xSemaphoreGive(s_lock);
}

void bus_reset_debug(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_dbg, 0, sizeof(s_dbg));
    s_last_pos = 0;
    xSemaphoreGive(s_lock);
}
