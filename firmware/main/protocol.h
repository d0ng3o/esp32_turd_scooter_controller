// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Taavi Laadung
//
// protocol.h - colon-framed one-wire bus protocol (see ../../PROTOCOL.md).
// Ports the exact framing/checksum/status layout proven in scooter_master.py
// and pi_sniff.py.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Frame delimiters
#define FRAME_SOF   0x3A   // ':'
#define FRAME_CR    0x0D
#define FRAME_LF    0x0A

// Master -> slave message types
#define MSG_POLL        0x1A   // 50 Hz heartbeat: [throttle, flags]
#define MSG_SN_QUERY    0x1B
#define MSG_FW_QUERY    0x1D

// Slave -> master message types
#define MSG_STATUS      0x10   // 21-byte telemetry reply
#define MSG_SN_REPLY    0x11
#define MSG_FW_REPLY    0x13

// Poll flag byte bits (PROTOCOL.md 3.1)
#define FLAG_TAILLIGHT  0x08   // master keeps this set at all times
#define FLAG_STATUSREQ  0x10   // request a status reply this poll
#define FLAG_HEADLIGHT  0x40   // set while the wheel is moving

#define STATUS_LEN      21     // payload length of a 0x10 status frame
#define POLL_FRAME_LEN  8      // :1A 02 TT FF CK CR LF

// XOR checksum: ':' ^ TYPE ^ LEN ^ DATA[0..len-1]  (PROTOCOL.md 2)
static inline uint8_t xor_chk(uint8_t type, const uint8_t *data, uint8_t len)
{
    uint8_t c = (uint8_t)(FRAME_SOF ^ type ^ len);
    for (uint8_t i = 0; i < len; i++) {
        c ^= data[i];
    }
    return c;
}

// Decoded telemetry (PROTOCOL.md 4.2). 16-bit fields are big-endian.
typedef struct {
    uint16_t temp1;        // raw controller/FET temperature
    uint16_t temp2;        // raw second temperature (motor?)
    uint16_t current_cA;   // battery current, 0.01 A units
    uint8_t  charge_cA;    // charge current, 0.01 A units
    uint16_t soc;          // state of charge, percent
    uint8_t  status_bit;   // offset 10, 0/1 (leans 'moving')
    uint8_t  speed;        // wheel speed reading (0..~156 limited)
    uint32_t odometer;     // u24, ~1 count per wheel revolution
    uint8_t  state_flags;  // offset 18 motion-state bits
    uint8_t  margin;       // offset 20 load-sensitive voltage margin
    int64_t  t_us;         // esp_timer timestamp of this decode
    bool     valid;        // true once at least one status has decoded
} telemetry_t;

// Decode the 21 payload bytes of a status frame into *t (sets t->valid, t->t_us).
void protocol_decode_status(const uint8_t *payload, telemetry_t *t);

// Build an 8-byte poll frame into buf (must hold POLL_FRAME_LEN). Returns length.
size_t protocol_build_poll(uint8_t *buf, uint8_t throttle, uint8_t flags);
