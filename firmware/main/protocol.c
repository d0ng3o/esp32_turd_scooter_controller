// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung

#include "protocol.h"
#include "esp_timer.h"

void protocol_decode_status(const uint8_t *d, telemetry_t *t)
{
    t->temp1      = (uint16_t)(d[0] << 8) | d[1];
    t->temp2      = (uint16_t)(d[2] << 8) | d[3];
    t->current_cA = (uint16_t)(d[4] << 8) | d[5];
    // d[6] always 0
    t->charge_cA  = d[7];
    t->soc        = (uint16_t)(d[8] << 8) | d[9];
    t->status_bit = d[10];
    // d[11] always 0
    t->speed      = d[12];
    // d[13] const 0x0E, d[14] always 0
    t->odometer   = ((uint32_t)d[15] << 16) | ((uint32_t)d[16] << 8) | d[17];
    t->state_flags = d[18];
    // d[19] const 0x10
    t->margin     = d[20];
    t->t_us       = esp_timer_get_time();
    t->valid      = true;
}

size_t protocol_build_poll(uint8_t *buf, uint8_t throttle, uint8_t flags)
{
    buf[0] = FRAME_SOF;
    buf[1] = MSG_POLL;
    buf[2] = 0x02;            // LEN
    buf[3] = throttle;        // DATA[0]
    buf[4] = flags;           // DATA[1]
    buf[5] = xor_chk(MSG_POLL, &buf[3], 2);
    buf[6] = FRAME_CR;
    buf[7] = FRAME_LF;
    return POLL_FRAME_LEN;
}
