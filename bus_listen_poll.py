#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
"""Poll at 50 Hz with throttle 0 for N minutes and log every non-echo byte."""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
MINUTES = float(sys.argv[2]) if len(sys.argv) > 2 else 10


def colon(ftype, data):
    body = bytes([0x3A, ftype, len(data)]) + bytes(data)
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x, 0x0D, 0x0A])


ser = serial.Serial(PORT, 115200, timeout=0)
log = open("logs/listen_poll.txt", "a", buffering=1)
t0 = time.perf_counter()
expect = bytearray()
foreign = bytearray()
n = 0
echo_ok = 0
next_t = t0
next_sum = t0 + 10
end = t0 + MINUTES * 60
log.write(f"--- start {time.strftime('%H:%M:%S')} ---\n")
while time.perf_counter() < end:
    now = time.perf_counter()
    if now >= next_t:
        next_t += 0.02
        n += 1
        f = colon(0x1A, bytes([0, 0x08 | (0x10 if n % 5 == 0 else 0)]))
        ser.write(f)
        expect += f
    chunk = ser.read(4096)
    for b in chunk:
        if expect and expect[0] == b:
            del expect[0]
            echo_ok += 1
        else:
            foreign.append(b)
    if foreign and (len(foreign) > 200 or not chunk):
        log.write(f"{now - t0:9.3f} RX {bytes(foreign).hex()}\n")
        foreign.clear()
    if now >= next_sum:
        next_sum += 10
        log.write(f"{now - t0:9.3f} SUMMARY polls={n} echo_bytes={echo_ok} unechoed={len(expect)}\n")
        if len(expect) > 1000:
            expect.clear()
    rem = next_t - time.perf_counter()
    if rem > 0.002:
        time.sleep(rem - 0.0015)
log.write(f"--- end {time.strftime('%H:%M:%S')} ---\n")
ser.close()
