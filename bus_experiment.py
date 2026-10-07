#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
"""
bus_experiment.py - scripted attempts to get the motor controller talking.

Runs several phases on COM4 and reports, per phase, every byte received that
is NOT the echo of our own transmission. Nothing interactive; throttle is
always 0.
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
CAPTURE = "captures/cap115200.bin.txt"


def colon(ftype, data):
    body = bytes([0x3A, ftype, len(data)]) + bytes(data)
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x, 0x0D, 0x0A])


class Bus:
    def __init__(self):
        self.ser = serial.Serial(PORT, 115200, timeout=0)
        self.expect = bytearray()
        self.foreign = bytearray()
        self.foreign_t = []
        self.t0 = time.perf_counter()
        self.log = open("logs/experiment_raw.txt", "a", buffering=1)

    def now(self):
        return time.perf_counter() - self.t0

    def send(self, b):
        self.ser.write(b)
        self.expect += b
        self.log.write(f"{self.now():9.4f} TX {b.hex()}\n")

    def pump(self):
        chunk = self.ser.read(4096)
        if not chunk:
            return
        for byte in chunk:
            if self.expect and self.expect[0] == byte:
                del self.expect[0]
            else:
                self.foreign.append(byte)
                self.foreign_t.append(self.now())
        if self.foreign:
            pass

    def flush_foreign(self, label):
        if self.foreign:
            print(f"   >>> {label}: {len(self.foreign)} foreign bytes: {bytes(self.foreign).hex()}")
            self.log.write(f"{self.now():9.4f} RX({label}) {bytes(self.foreign).hex()}\n")
        else:
            print(f"   {label}: no foreign bytes")
        unechoed = len(self.expect)
        if unechoed:
            print(f"   ({unechoed} of our bytes never echoed back)")
            self.expect.clear()
        self.foreign.clear()
        self.foreign_t.clear()

    def wait(self, seconds):
        end = time.perf_counter() + seconds
        while time.perf_counter() < end:
            self.pump()
            time.sleep(0.001)


def poll_phase(bus, label, seconds, flags_fn, extras=None, period=0.020):
    print(f"\n== {label} ({seconds:.0f} s)")
    n = 0
    next_t = time.perf_counter()
    end = next_t + seconds
    last_extra = 0
    while time.perf_counter() < end:
        now = time.perf_counter()
        if now >= next_t:
            next_t += period
            n += 1
            bus.send(colon(0x1A, bytes([0, flags_fn(n)])))
            if extras and now - last_extra > 2.0 and n % 5 != 0:
                last_extra = now
                for t, d in extras:
                    bus.send(colon(t, d))
        bus.pump()
        rem = next_t - time.perf_counter()
        if rem > 0.002:
            time.sleep(rem - 0.0015)
    bus.wait(0.3)
    bus.flush_foreign(label)


def replay_phase(bus, t_from, t_to):
    """Replay the original master's bytes from the capture with original timing.
    Controller frames (colon types 10/11/13/14) are removed; everything else
    (READY banner, 55AA/BEEF/5AA5 probes, colon polls and queries) is sent."""
    print(f"\n== replay of original master traffic, capture t={t_from}..{t_to}")
    data = bytearray()
    times = []
    for line in open(CAPTURE):
        t, n, h = line.split()
        t = float(t)
        if t < t_from - 1 or t > t_to + 1:
            continue
        b = bytes.fromhex(h)
        data += b
        times += [t] * len(b)
    # split into frames, keep master ones
    out = []
    i = 0
    while i < len(data):
        t = times[i]
        if data[i] == 0x3A and i + 3 <= len(data):
            L = data[i + 2]
            end = i + 3 + L + 3
            if end <= len(data) and data[end - 2:end] == b"\r\n":
                if data[i + 1] not in (0x10, 0x11, 0x13, 0x14):
                    out.append((t, bytes(data[i:end])))
                i = end
                continue
            j = data.find(b"\r\n", i)
            if j != -1 and j - i < 40:          # READY banner style line
                out.append((t, bytes(data[i:j + 2])))
                i = j + 2
                continue
        if data[i:i + 2] in (b"\x55\xaa", b"\xbe\xef", b"\x5a\xa5") and i + 3 <= len(data):
            L = data[i + 2]
            # 55AA/BEEF: LEN+1 payload bytes; 5AA5: LEN+2 (src dst cmd arg) + payload
            end = i + 3 + L + 1 + 2 if data[i] != 0x5A else i + 3 + 4 + L + 2
            if end <= len(data):
                fr = bytes(data[i:end])
                # keep only master-originated probes (payload starts 0x20 or 3E/21 with 5AA5)
                out.append((t, fr))
                i = end
                continue
        i += 1
    out = [(t, f) for t, f in out if t_from <= t <= t_to]
    print(f"   {len(out)} frames to replay")
    if not out:
        return
    base_cap = out[0][0]
    base_now = time.perf_counter()
    kinds = {}
    for t, f in out:
        target = base_now + (t - base_cap)
        while time.perf_counter() < target:
            bus.pump()
            time.sleep(0.0005)
        bus.send(f)
        k = f[:2].hex() if f[0] != 0x3A else f"colon {f[1]:02x}"
        kinds[k] = kinds.get(k, 0) + 1
        bus.pump()
    print(f"   sent: {kinds}")
    bus.wait(0.5)
    bus.flush_foreign("replay")


def main():
    bus = Bus()
    print(f"port {PORT} open; idle listen 2 s")
    bus.wait(2.0)
    bus.flush_foreign("idle")

    poll_phase(bus, "A plain polls, status request every 5th", 8,
               lambda n: 0x08 | (0x10 if n % 5 == 0 else 0))
    poll_phase(bus, "B polls + SN/FW/1E/1F/20 every 2 s", 8,
               lambda n: 0x08 | (0x10 if n % 5 == 0 else 0),
               extras=[(0x1B, b"SN"), (0x1D, b"FW"), (0x1E, b"\x20\x10"), (0x1F, b"\x02\xF4"), (0x20, b"\x00\x87")])
    poll_phase(bus, "C status request on every poll", 5, lambda n: 0x18)
    poll_phase(bus, "D headlight flag set", 5, lambda n: 0x48 | (0x10 if n % 5 == 0 else 0))
    poll_phase(bus, "E flags 0x00/0x10 only", 5, lambda n: 0x10 if n % 5 == 0 else 0x00)
    poll_phase(bus, "F slow polls 10 Hz", 5, lambda n: 0x18, period=0.1)
    replay_phase(bus, 48.0, 72.0)
    poll_phase(bus, "G plain polls again after replay", 8,
               lambda n: 0x08 | (0x10 if n % 5 == 0 else 0))
    print("\ndone; raw log appended to logs/experiment_raw.txt")
    bus.ser.close()


if __name__ == "__main__":
    main()
