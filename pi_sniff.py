#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
"""
pi_sniff.py - passive one-wire bus logger for a Raspberry Pi ride recorder.

RECEIVE-ONLY. The original control board stays connected and acts as master;
this just listens on the data line and logs every status frame, so we capture
telemetry under real riding load (throttle, speed, current, and hopefully the
still-unknown pack-voltage and temperature fields).

Wiring (Pi):
    scooter GND -> Pi GND (pin 6)
    data line   -> [1k] -> Pi GPIO15 / RXD (pin 10)      (RX only, no TX)
    Power the Pi from its own battery/power bank. Never connect the pack (~41V)
    or a 5V rail to a GPIO.

The Pi UART must be free: enable_uart=1, dtoverlay=disable-bt in config.txt, no
serial console in cmdline.txt, and serial-getty disabled (the installer does the
last one).

Standard library only - no pyserial, so nothing to install on the Pi.
Linux-only (uses termios).

Usage:  pi_sniff.py [--port /dev/serial0] [--out DIR] [--quiet]
Writes  <out>/ride_YYYYmmdd_HHMMSS.csv  (full 21-byte frame + decoded fields)
and appends drift notes to <out>/ride_notes.txt.
"""
import argparse
import csv
import os
import sys
import time

NBYTES = 21


def open_serial(port, baud=115200):
    """Open a tty at 8N1 raw using only the standard library (termios)."""
    import termios  # lazy: Linux-only, keeps this file importable on Windows
    speed = getattr(termios, f"B{baud}")
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = termios.tcgetattr(fd)
    iflag = 0
    oflag = 0
    lflag = 0
    cflag = termios.CS8 | termios.CREAD | termios.CLOCAL
    ispeed = ospeed = speed
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, ispeed, ospeed, cc])
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def read_serial(fd, n=512, timeout=0.2):
    import select
    r, _, _ = select.select([fd], [], [], timeout)
    if not r:
        return b""
    try:
        return os.read(fd, n)
    except BlockingIOError:
        return b""


def be16(d, i):
    return (d[i] << 8) | d[i + 1]


def decode(d):
    odo = (d[15] << 16) | (d[16] << 8) | d[17]
    return {
        "v1": be16(d, 0),
        "v2": be16(d, 2),
        "current_A": be16(d, 4) / 100.0,
        "charge_A": d[7] / 100.0,
        "soc": be16(d, 8),
        "b10": d[10],
        "speed": d[12],
        "b13": d[13],
        "odo": odo,
        "b18": d[18],
        "b19": d[19],
        "b20": d[20],
    }


class Parser:
    """Extract colon frames; also flags the raw READY banner."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, chunk):
        out = []
        self.buf += chunk
        if b"READY" in self.buf:
            out.append(("ready", None))
        while self.buf:
            if self.buf[0] != 0x3A:
                del self.buf[0]
                continue
            if len(self.buf) < 3:
                break
            length = self.buf[2]
            if length > 64:
                del self.buf[0]
                continue
            end = 3 + length + 3
            if len(self.buf) < end:
                break
            body = self.buf[:end]
            x = 0
            for b in body[: 3 + length]:
                x ^= b
            if body[3 + length] == x and body[end - 2] == 0x0D and body[end - 1] == 0x0A:
                out.append(("frame", (body[1], bytes(body[3 : 3 + length]))))
                del self.buf[:end]
            else:
                del self.buf[0]
        if len(self.buf) > 512:      # never grow without bound on a noisy line
            del self.buf[:-64]
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/serial0")
    ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(args.out, f"ride_{stamp}.csv")
    seq = 1
    while os.path.exists(csv_path):   # clock may repeat on a headless Pi; never overwrite
        seq += 1
        csv_path = os.path.join(args.out, f"ride_{stamp}_{seq}.csv")
    note_path = os.path.join(args.out, "ride_notes.txt")

    fd = None
    while fd is None:                # device may not be ready right at boot
        try:
            fd = open_serial(args.port)
        except Exception as e:  # noqa: BLE001
            print(f"waiting for {args.port}: {e}", flush=True)
            time.sleep(2)

    cf = open(csv_path, "w", newline="")
    w = csv.writer(cf)
    w.writerow(["t"] + [f"b{i}" for i in range(NBYTES)]
               + ["v1", "v2", "current_A", "charge_A", "soc", "speed", "odo", "b20"])
    note = open(note_path, "a", buffering=1)

    def log(msg):
        line = f"{stamp} {time.time()-t0:8.1f}  {msg}"
        note.write(line + "\n")
        note.flush()
        os.fsync(note.fileno())
        if not args.quiet:
            print(line, flush=True)

    parser = Parser()
    t0 = time.time()
    log(f"ride log start, port {args.port}, file {csv_path}")
    baseline = None
    mins = [255] * NBYTES
    maxs = [0] * NBYTES
    n = 0
    last_sync = time.time()
    last_report = time.time()
    while True:
        chunk = read_serial(fd)
        for kind, payload in parser.feed(chunk):
            if kind == "ready":
                log("controller READY banner")
                continue
            ftype, data = payload
            if ftype == 0x10 and len(data) == NBYTES:
                n += 1
                dec = decode(data)
                w.writerow([round(time.time() - t0, 2)] + list(data)
                           + [dec["v1"], dec["v2"], dec["current_A"], dec["charge_A"],
                              dec["soc"], dec["speed"], dec["odo"], dec["b20"]])
                if baseline is None:
                    baseline = list(data)
                    log("baseline: " + data.hex())
                for i in range(NBYTES):
                    mins[i] = min(mins[i], data[i])
                    maxs[i] = max(maxs[i], data[i])
        now = time.time()
        if now - last_sync > 2:      # power cut loses at most ~2 s of data
            last_sync = now
            cf.flush()
            os.fsync(cf.fileno())
        if now - last_report > 30:
            last_report = now
            drift = [f"b{i}:{mins[i]}-{maxs[i]}" for i in range(NBYTES) if mins[i] != maxs[i]]
            log(f"{n} frames | drift: " + (", ".join(drift) if drift else "none"))


if __name__ == "__main__":
    main()
