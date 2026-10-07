#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
"""
bus_soak.py - long idle soak logger.

Polls the controller at 50 Hz with throttle 0 and the HEADLIGHT flag on, logs
every status frame, and every 60 s prints which status bytes have drifted from
their baseline (first frame) with min/max. Meant to catch slow fields like
temperature that only move over many minutes.

Usage:  python bus_soak.py [COM4] [seconds]
"""
import csv
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
DURATION = float(sys.argv[2]) if len(sys.argv) > 2 else 3600.0
POLL = 0.020
NBYTES = 21


def colon(ftype, data):
    body = bytes([0x3A, ftype, len(data)]) + bytes(data)
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x, 0x0D, 0x0A])


def parse(buf, out):
    """Extract complete status (0x10) payloads from buf; return leftover."""
    i = 0
    n = len(buf)
    while i < n:
        if buf[i] != 0x3A:
            i += 1
            continue
        if i + 3 > n:
            break
        length = buf[i + 2]
        end = i + 3 + length + 3
        if length > 64:
            i += 1
            continue
        if end > n:
            break
        body = buf[i:end]
        x = 0
        for b in body[: 3 + length]:
            x ^= b
        if body[3 + length] == x and body[end - 2] == 0x0D and body[end - 1] == 0x0A:
            if body[1] == 0x10 and length == NBYTES:
                out.append(bytes(body[3 : 3 + length]))
            i = end
        else:
            i += 1
    return buf[i:]


def main():
    ser = serial.Serial(PORT, 115200, timeout=0)
    raw = open("logs/soak_raw.csv", "w", newline="")
    w = csv.writer(raw)
    w.writerow(["t"] + [f"b{i}" for i in range(NBYTES)])
    note = open("logs/soak_notes.txt", "w", buffering=1)

    def log(msg):
        line = f"{time.time()-t0:8.1f}  {msg}"
        note.write(line + "\n")
        print(line, flush=True)

    t0 = time.time()
    log(f"soak start on {PORT} for {DURATION:.0f}s, headlight ON, throttle 0")
    baseline = None
    mins = [255] * NBYTES
    maxs = [0] * NBYTES
    buf = bytearray()
    frames = []
    n = 0
    echo = bytearray()
    next_poll = time.perf_counter()
    next_report = t0 + 60
    end = t0 + DURATION
    nframes = 0
    while time.time() < end:
        now = time.perf_counter()
        if now >= next_poll:
            next_poll += POLL
            n += 1
            flags = 0x08 | 0x40                     # tail + headlight
            if n % 5 == 0:
                flags |= 0x10                       # request status
            f = colon(0x1A, bytes([0, flags]))
            ser.write(f)
            echo += f
        chunk = ser.read(4096)
        if chunk:
            # strip our own echo byte-for-byte, keep the rest
            keep = bytearray()
            for b in chunk:
                if echo and echo[0] == b:
                    del echo[0]
                else:
                    keep.append(b)
            buf += keep
            frames.clear()
            buf = bytearray(parse(buf, frames))
            for fr in frames:
                nframes += 1
                w.writerow([round(time.time() - t0, 2)] + list(fr))
                if baseline is None:
                    baseline = list(fr)
                    log("baseline: " + fr.hex())
                for i in range(NBYTES):
                    mins[i] = min(mins[i], fr[i])
                    maxs[i] = max(maxs[i], fr[i])
        if time.time() >= next_report:
            next_report += 60
            drift = []
            for i in range(NBYTES):
                if mins[i] != maxs[i]:
                    drift.append(f"b{i}:{mins[i]}-{maxs[i]}(now {frames[-1][i] if frames else '?'})")
            log(f"{nframes} frames | drifting bytes: " + (", ".join(drift) if drift else "none"))
    log(f"soak done: {nframes} status frames")
    # final per-byte range vs baseline
    for i in range(NBYTES):
        if mins[i] != maxs[i]:
            log(f"  b{i}: baseline {baseline[i]} range {mins[i]}..{maxs[i]}")
    ser.close()
    raw.close()


if __name__ == "__main__":
    main()
