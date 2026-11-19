#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
"""
bus_query_probe.py - probe the controller's ASCII named-query channel.

The motor controller answers short text queries sent with frame type 0x1B:
the original board asked "SN" (serial) and got a 0x11 reply, and "FW" (firmware,
via 0x1D) and got a 0x13 reply. The query handler clearly parses an arbitrary
ASCII tag, so this script walks a wordlist of candidate tags through 0x1B and
records anything that comes back. The hope: a tag that returns battery / cell /
temperature / range data the status frame never carries.

This is the ONLY no-hardware avenue left for richer telemetry (see
BMS_PROTOCOL.md / PROTOCOL.md): everything else needs tapping the pack's BMS
line, and the ESC is potted + read-protected so its firmware can't be inspected.

SAFETY / why this is low risk:
  - It sends ONLY the documented query opcode 0x1B, with different *string*
    payloads. It never touches unknown opcodes and never writes config
    (no 0x1E/0x1F/0x20), so it can't change controller settings or brick it.
  - Throttle is held at 0 the entire time. An unrecognised tag is expected to
    simply get no reply (like late FW queries already do).
  - Still: run with the wheel OFF the ground, control board disconnected from
    the data line, as a matter of habit.

Wiring + echo handling are identical to scooter_master.py (single wire, the
controller pulls up through ~740 ohm; drive TX through 100 ohm; RX sees our own
echo, which we strip). The 50 Hz poll heartbeat runs throughout so the
controller stays responsive while we interleave the queries.

Output:
  - logs/<ts>_probe_raw.txt  - every TX/RX frame with the tag it belongs to
  - a summary table at the end: each tag -> reply (type, hex, ascii) or "-"
    Tags whose reply differs from the SN/FW baseline are flagged with "<<<".

Usage:
  python bus_query_probe.py --port COM4
  python bus_query_probe.py --port COM4 --tags BAT,VOL,CELL,SOC   # custom list
  python bus_query_probe.py --port COM4 --lower                    # also try lowercase
  python bus_query_probe.py --port COM4 --window 0.4 --repeat 2
"""
import argparse
import collections
import os
import sys
import time

import serial

POLL_PERIOD = 0.020
STATUS_EVERY = 5
OUR_TYPES = {0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20}

# Candidate tags to try, grouped by theme. SN/FW are positive controls (known to
# work). Keep them short and uppercase, like the two the stock board used.
DEFAULT_TAGS = [
    # positive controls (known-good)
    "SN", "FW",
    # identity / version
    "HW", "SW", "VER", "VERSION", "MODEL", "TYPE", "ID", "UID", "PN", "REV",
    "MFG", "MFR", "DATE", "LOT", "HWV", "SWV",
    # battery pack voltage
    "BAT", "BATT", "BT", "BV", "VB", "VBAT", "VOL", "VOLT", "VLT", "PV", "PACK",
    "VP", "V",
    # cells
    "CEL", "CELL", "CELLS", "CV", "VC", "C1", "CELLV",
    # current / power
    "CUR", "CRT", "AMP", "IB", "IBAT", "I", "PWR", "POWER", "W",
    # capacity / charge / state
    "CAP", "AH", "MAH", "WH", "SOC", "SOH", "CHG", "CHRG", "CHARGE",
    # cycles
    "CYC", "CYCLE", "CC",
    # temperature
    "TMP", "TEMP", "T", "T1", "T2", "TC", "TM", "TE",
    # range / odometer / distance
    "RNG", "RANGE", "ODO", "KM", "MI", "DIST", "MILE", "TRIP",
    # status / diagnostics / faults
    "ST", "STAT", "STATUS", "ERR", "FLT", "FAULT", "DIAG", "DBG", "DEBUG",
    "LOG", "INFO", "HEALTH",
    # power management / sleep (long shot, but this is the whole point)
    "SLP", "SLEEP", "OFF", "PARK", "STBY", "IDLE", "SHDN", "STOP", "LP", "LPM",
    # limits / config (read-style words only; we do NOT write)
    "LIM", "LIMIT", "SPD", "SPEED", "MAX", "CFG", "CONF", "PARAM", "PAR",
    "GET", "BMS",
    # generic "dump everything"
    "ALL", "DAT", "DATA", "DUMP", "HELP", "MENU", "CMD", "?",
]


def build(ftype, data):
    body = bytes([0x3A, ftype, len(data)]) + bytes(data)
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x, 0x0D, 0x0A])


class Parser:
    """Feeds bytes, yields ('frame', type, data) or ('junk', byte, None)."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, chunk):
        self.buf += chunk
        out = []
        while self.buf:
            if self.buf[0] != 0x3A:
                out.append(("junk", self.buf[0], None))
                del self.buf[0]
                continue
            if len(self.buf) < 3:
                break
            length = self.buf[2]
            if length > 64:
                out.append(("junk", self.buf[0], None))
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
                out.append(("frame", body[1], bytes(body[3 : 3 + length])))
                del self.buf[:end]
            else:
                out.append(("junk", self.buf[0], None))
                del self.buf[0]
        return out


def ascii_of(data):
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


class Prober:
    def __init__(self, port, baud, tags, window, repeat):
        self.ser = serial.Serial(port, baud, timeout=0)
        self.parser = Parser()
        self.expect_echo = collections.deque()
        self.echo_ok = 0
        self.echo_bad = 0
        self.status_count = 0
        self.alive = False

        # Build the probe plan: each tag, repeated `repeat` times.
        self.plan = []
        for _ in range(repeat):
            self.plan.extend(tags)
        self.plan_i = 0
        self.window = window
        self.cur_tag = None
        self.window_until = 0.0
        self.replies = collections.OrderedDict()   # tag -> list of (type, data)
        for t in tags:
            self.replies.setdefault(t, [])

        self.t0 = time.perf_counter()
        os.makedirs("logs", exist_ok=True)
        stamp = time.strftime("%Y%m%d_%H%M%S")
        self.rawlog = open(f"logs/{stamp}_probe_raw.txt", "w", buffering=1)
        self.rawlog.write("# bus_query_probe raw log: TX/RX frames tagged by the "
                          "query in flight\n")

    def now(self):
        return time.perf_counter() - self.t0

    def send(self, ftype, data):
        self.ser.write(build(ftype, data))
        self.expect_echo.append((ftype, bytes(data)))
        self.rawlog.write(f"{self.now():10.4f} TX {ftype:02x} {bytes(data).hex()} "
                          f"tag={self.cur_tag}\n")

    def pump(self):
        chunk = self.ser.read(4096)
        if not chunk:
            return
        for kind, a, b in self.parser.feed(chunk):
            if kind == "junk":
                continue
            ftype, data = a, b
            # strip our own echo
            if ftype in OUR_TYPES:
                exp = self.expect_echo.popleft() if self.expect_echo else None
                if exp == (ftype, data):
                    self.echo_ok += 1
                else:
                    self.echo_bad += 1
                continue
            if ftype == 0x10:                       # status: proves it's alive
                self.status_count += 1
                self.alive = True
                continue
            # Anything else is a genuine reply. Attribute it to the tag in flight.
            self.rawlog.write(f"{self.now():10.4f} RX {ftype:02x} {data.hex()} "
                              f"|{ascii_of(data)}| tag={self.cur_tag}\n")
            if self.cur_tag is not None:
                self.replies[self.cur_tag].append((ftype, data))

    def selfcheck(self):
        self.ser.reset_input_buffer()
        f = build(0x1A, bytes([0, 0x08]))
        self.ser.write(f)
        time.sleep(0.05)
        got = self.ser.read(4096)
        print(f"selfcheck: sent {f.hex()}, received {got.hex() or 'NOTHING'}")
        if not got:
            print("  -> RX sees nothing of our TX; check wiring before trusting results.")
        self.ser.reset_input_buffer()

    def run(self):
        self.selfcheck()
        print(f"probing {len(self.replies)} tags x{self.plan.count(self.plan[0]) if self.plan else 0} "
              f"(window {self.window:.2f}s). Ctrl-C to stop early.\n")
        n = 0
        next_poll = time.perf_counter()
        done = False
        try:
            while not done:
                now = time.perf_counter()
                # advance the probe window
                if self.cur_tag is None or now >= self.window_until:
                    if self.plan_i < len(self.plan):
                        self.cur_tag = self.plan[self.plan_i]
                        self.plan_i += 1
                        self.window_until = now + self.window
                        self.send(0x1B, self.cur_tag.encode("ascii"))
                        sys.stdout.write(f"\r  [{self.plan_i:3d}/{len(self.plan)}] "
                                         f"query {self.cur_tag:<8} ".ljust(40))
                        sys.stdout.flush()
                    else:
                        # drain one last window then finish
                        self.cur_tag = None
                        time.sleep(self.window)
                        self.pump()
                        done = True
                        continue
                # heartbeat poll
                if now >= next_poll:
                    next_poll += POLL_PERIOD
                    if now - next_poll > 0.5:
                        next_poll = now + POLL_PERIOD
                    n += 1
                    flags = 0x08 | (0x10 if n % STATUS_EVERY == 0 else 0)
                    self.send(0x1A, bytes([0, flags]))
                self.pump()
                rem = next_poll - time.perf_counter()
                if rem > 0.002:
                    time.sleep(rem - 0.0015)
        except KeyboardInterrupt:
            print("\n(interrupted)")
        finally:
            for _ in range(10):
                self.ser.write(build(0x1A, bytes([0, 0x08])))
                time.sleep(POLL_PERIOD)
            self.ser.close()
            self.summary()
            self.rawlog.close()

    def summary(self):
        # Baseline = whatever SN/FW returned, so novel replies stand out.
        baseline = set()
        for ctrl in ("SN", "FW"):
            for ftype, data in self.replies.get(ctrl, []):
                baseline.add((ftype, bytes(data)))

        print("\n\n==== query probe summary ====")
        print(f"status frames seen: {self.status_count}  echo ok/bad: "
              f"{self.echo_ok}/{self.echo_bad}  "
              f"(channel {'LIVE' if self.alive else 'NOT CONFIRMED'})")
        if not any(self.replies.get(c) for c in ("SN", "FW")):
            print("WARNING: neither SN nor FW replied -- the query channel looks "
                  "closed on this board, so 'no reply' below is inconclusive.")
        print(f"{'tag':<10} {'reply':<6} {'data (hex)':<34} ascii")
        print("-" * 72)
        hits = 0
        for tag, rl in self.replies.items():
            if not rl:
                print(f"{tag:<10} {'-':<6}")
                continue
            for ftype, data in rl:
                novel = (ftype, bytes(data)) not in baseline and tag not in ("SN", "FW")
                mark = "  <<< NEW" if novel else ""
                if novel:
                    hits += 1
                h = data.hex()
                h = h if len(h) <= 32 else h[:29] + "..."
                print(f"{tag:<10} 0x{ftype:02x}   {h:<34} {ascii_of(data)}{mark}")
        print("-" * 72)
        print(f"{hits} tag(s) returned something beyond the SN/FW baseline."
              if hits else
              "No tag returned anything beyond SN/FW -- the controller exposes "
              "no extra string fields. The bus is exhausted.")


def main():
    ap = argparse.ArgumentParser(description="Probe the controller's 0x1B ASCII query channel")
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tags", help="comma-separated tag list (overrides the built-in)")
    ap.add_argument("--lower", action="store_true", help="also try a lowercase copy of each tag")
    ap.add_argument("--window", type=float, default=0.30, help="seconds to listen after each query")
    ap.add_argument("--repeat", type=int, default=1, help="passes over the whole tag list")
    args = ap.parse_args()

    tags = ([t.strip() for t in args.tags.split(",") if t.strip()]
            if args.tags else list(DEFAULT_TAGS))
    if args.lower:
        seen = set(tags)
        for t in list(tags):
            lo = t.lower()
            if lo != t and lo not in seen:
                tags.append(lo)
                seen.add(lo)

    print(__doc__)
    Prober(args.port, args.baud, tags, args.window, max(1, args.repeat)).run()


if __name__ == "__main__":
    main()
