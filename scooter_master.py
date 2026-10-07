#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Taavi Laadung
"""
scooter_master.py - act as the scooter's control board on the one-wire bus.

Talks to the motor controller with the colon protocol reverse-engineered on
2026-09-11:

    ':' TYPE LEN DATA[LEN] CHK CR LF        CHK = XOR of ':' TYPE LEN DATA

    0x1A poll, 50 Hz, DATA = [throttle 0..255, flags]
         flags: 0x08 always set (tail light), 0x10 = request status
                (every 5th poll), 0x40 = headlight
    0x10 status reply, 21 bytes, big-endian 16-bit fields
    0x1B "SN" -> 0x11 serial numbers      0x1D "FW" -> 0x13 firmware string
    0x1E [20 10] -> 0x14 echo, 0x1F [02 F4], 0x20 [00 87]: one-shot boot frames

Wiring (control board DISCONNECTED from the data line), verified 2026-09-11:
    adapter GND -> scooter GND
    adapter RX  -> data line (direct)
    adapter TX  -> 100 ohm resistor -> data line
    The controller holds the line high through about 740 ohm, so a diode-OR
    from TX (tried 1N4007) only reaches ~1.3 V low and the controller ignores
    us; 1 kohm in series gives 1.9 V, also ignored; 100 ohm gives a valid low
    and works. The resistor limits contention current (~30 mA for ~2 ms)
    while the controller replies against our idle-high TX. For a permanent
    install use an open-drain buffer (74HC07 / 74LVC07) instead.

Because RX sees our own transmissions, every frame we send comes back as an
echo. The script verifies each echo; a mismatch means a bus collision.

Keys (hold-to-run by default, throttle drops to 0 within ~250 ms of release):
    w          hold to apply throttle at the current level
    1..9, 0    set level 10%..90%, 100%        + / -   level +-5
    space, b   throttle 0 now, latch off
    l          toggle latch (throttle stays applied WITHOUT holding w)
    h          headlight: auto (on when moving) -> on -> off
    n / v      send SN / FW query
    e / f / g  send boot one-shots 0x1E, 0x1F, 0x20
    x          raw frame: then type  TYPE HEXDATA  and Enter, e.g.  1e 2010

    Experiments (see EXPERIMENTS below):
    F          flag scan: sweep candidate poll flag bits (0,2,5,7). Run at FULL
               throttle + spinning. (Bit 1 / 0x02 is excluded: it CUTS drive.)
    z          hold one flag byte until cleared: type a hex byte, e.g.
               0c (bit2), 28 (bit5), 88 (bit7). Empty input or k clears it.
    c          config fuzz: type  TYPE p1 p2 ...  e.g.  1f 02f4 0300 04b0 05dc
               (sends each payload, then restores the factory value)
    B          boot inject: type overrides e.g.  1f=07d0 20=00ff  then
               power-cycle the controller. Polling keeps running (tail light
               stays on); when the READY banner arrives the script injects the
               boot handshake (SN, FW, 1E, 1F, 20) with your values. Use this to
               test config that the controller only reads at startup.
    k          cancel the running sequence / clear a held flag byte

    p          pause/resume polling (see how the controller reacts to silence)
    q          quit (sends zero-throttle polls first)

EXPERIMENTS (looking for the speed limiter and kick-start settings)

  The controller is a Nanjing Lishui LSW6G-RF-FOC with Bird's own firmware.
  The limiter (~25 km/h, speed reading caps at 156) and kick-to-start are
  controller config, not per-frame commands. The original board only ever
  wrote three config-looking frames at boot: 0x1E=2010, 0x1F=02F4, 0x20=0087.
  We previously re-sent those SAME values, so nothing changed. The untried
  experiment is to send DIFFERENT values, and to try the poll flag bits the
  board never set.

  SAFETY: config writes may persist and may make the controller behave
  unexpectedly (sudden power, changed limits). Always: wheel OFF the ground,
  current-limited bench supply if possible, and note that 'c' auto-restores
  the factory value after each sweep. Factory values are logged at startup
  (grep FACTORY in the raw log) so you can always write them back by hand.

  Flag scan (F): spin the wheel and keep it spinning through the whole scan.
  Watch the speed reading. If it climbs past ~156 with a bit set, that bit
  lifts the limiter. If the motor engages while the wheel is NOT spinning with
  a bit set, that bit disables kick-start.

  Config fuzz (c): step a config register. Examples to try, one line at a time:
     1f 02f4 0384 03e8 044c 04b0 0514      (0x1F around 756: 900,1000,...,1300)
     20 0087 0064 00a0 00c8 00ff           (0x20 around 135)
     1e 2010 2020 2040 2080 20ff           (0x1E high nibble / low byte)
  After each line, watch whether the limiter or kick-start behavior changed on
  a spin+throttle test, then move to the next.

Logs go to ./logs/<timestamp>_raw.txt (every frame, both directions) and
./logs/<timestamp>_status.csv (decoded status frames).
"""
import argparse
import collections
import csv
import os
import sys
import time

import serial

try:
    import msvcrt
except ImportError:  # not Windows
    msvcrt = None

POLL_PERIOD = 0.020
STATUS_EVERY = 5
HOLD_TIMEOUT = 0.4
BOOT_INJECT_DELAY = 1.0       # seconds after the READY banner before injecting
WHEEL_CIRC_M = 0.70          # 222 mm wheel; odometer = 1 count per revolution
SPEED_KMH_PER_UNIT = 0.1609  # hypothesis: speed byte is in 0.1 mph

OUR_TYPES = {0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20}


def build(ftype, data):
    body = bytes([0x3A, ftype, len(data)]) + bytes(data)
    x = 0
    for b in body:
        x ^= b
    return body + bytes([x, 0x0D, 0x0A])


class Parser:
    """Feeds bytes, yields ("frame", type, data) or ("junk", byte, None)."""

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


def be16(d, i):
    return (d[i] << 8) | d[i + 1]


def decode_status(d):
    if len(d) < 21:
        return None
    odo = (d[15] << 16) | (d[16] << 8) | d[17]
    return {
        "v1_raw": be16(d, 0),          # 2970..3000, rises under load; not simply pack V
        "v2_raw": be16(d, 2),
        "current_A": be16(d, 4) / 100.0,  # 0.01 A (10 mA) units
        "b6": d[6],
        "charge_A": d[7] / 100.0,      # 0.01 A units; 0 with charger unplugged
        "soc_pct": be16(d, 8),
        "b10": d[10],
        "b11": d[11],
        "speed_raw": d[12],
        "speed_kmh_est": round(d[12] * SPEED_KMH_PER_UNIT, 1),
        "b13": d[13],
        "b14": d[14],
        "odo_rev": odo,
        "odo_km_est": round(odo * WHEEL_CIRC_M / 1000.0, 2),
        "b18": d[18],
        "b19": d[19],
        "b20": d[20],
    }


class Master:
    def __init__(self, port, baud, boot):
        self.ser = serial.Serial(port, baud, timeout=0)
        self.parser = Parser()
        self.level = 0
        self.hold_until = 0.0
        self.latch = False
        self.headlight_mode = "auto"
        self.paused = False
        self.extra = collections.deque()
        self.expect_echo = collections.deque()
        self.echo_ok = 0
        self.echo_bad = 0
        self.junk = 0
        self.status = None
        self.status_count = 0
        self.last_status_t = 0.0
        self.messages = collections.deque(maxlen=6)
        self.raw_mode = False
        self.raw_buf = ""
        self.raw_kind = None        # what the current raw-input line is for
        self.flag_override = None    # forces the poll flag byte during a scan
        self.boot_inject = None      # dict {type:data} to send after next READY
        self.boot_fire_at = None     # perf_counter time to fire the injection
        self._readytail = b""        # rolling tail for raw READY-banner detection
        self.seq = None              # active experiment sequence (deque of steps)
        self.seq_next = 0.0          # perf_counter time to advance to next step
        self.seq_label = ""
        self.t0 = time.perf_counter()
        os.makedirs("logs", exist_ok=True)
        stamp = time.strftime("%Y%m%d_%H%M%S")
        self.rawlog = open(f"logs/{stamp}_raw.txt", "w", buffering=1)
        self.csvf = open(f"logs/{stamp}_status.csv", "w", newline="")
        self.csv = None
        # Factory boot-frame values, as the original Bird board sent them.
        # Kept so a config fuzz can always be restored to stock.
        self.factory = {0x1E: b"\x20\x10", 0x1F: b"\x02\xF4", 0x20: b"\x00\x87"}
        self.rawlog.write("FACTORY boot frames (restore targets): "
                          + " ".join(f"{t:02x}={d.hex()}" for t, d in self.factory.items())
                          + "\n")
        if boot:
            for t, d in ((0x1B, b"SN"), (0x1D, b"FW"), (0x1E, b"\x20\x10"),
                         (0x1F, b"\x02\xF4"), (0x20, b"\x00\x87")):
                self.extra.append((t, d))

    # ---------- helpers ----------
    def now(self):
        return time.perf_counter() - self.t0

    def log(self, direction, ftype, data):
        self.rawlog.write(f"{self.now():10.4f} {direction} {ftype:02x} {data.hex()}\n")

    def send(self, ftype, data):
        f = build(ftype, data)
        self.ser.write(f)
        self.expect_echo.append((ftype, bytes(data)))
        self.log("TX", ftype, bytes(data))

    def throttle_now(self):
        if self.latch:
            return self.level
        return self.level if time.perf_counter() < self.hold_until else 0

    def headlight_on(self):
        if self.headlight_mode == "on":
            return True
        if self.headlight_mode == "off":
            return False
        return bool(self.status and self.status["speed_raw"] > 0)

    # ---------- receive ----------
    def pump(self):
        chunk = self.ser.read(4096)
        if not chunk:
            return
        # The controller's power-up banner (":\x1dREADY\r\n") is raw text, not a
        # checksummed frame, so the frame parser discards it. Detect it here on
        # the raw byte stream instead.
        combined = self._readytail + chunk
        if b"READY" in combined:
            self._readytail = b""
            self.on_ready()
        else:
            self._readytail = combined[-8:]
        for kind, a, b in self.parser.feed(chunk):
            if kind == "junk":
                self.junk += 1
                self.rawlog.write(f"{self.now():10.4f} JUNK {a:02x}\n")
                continue
            ftype, data = a, b
            if ftype in OUR_TYPES:
                exp = self.expect_echo.popleft() if self.expect_echo else None
                if exp == (ftype, data):
                    self.echo_ok += 1
                else:
                    self.echo_bad += 1
                    self.log("ECHO?", ftype, data)
                continue
            self.log("RX", ftype, data)
            if ftype == 0x10:
                st = decode_status(data)
                if st:
                    self.status = st
                    self.status_count += 1
                    self.last_status_t = time.perf_counter()
                    row = {"t": round(self.now(), 3), "throttle": self.throttle_now(), **st, "raw": data.hex()}
                    if self.csv is None:
                        self.csv = csv.DictWriter(self.csvf, fieldnames=list(row))
                        self.csv.writeheader()
                    self.csv.writerow(row)
            else:
                printable = all(32 <= c < 127 for c in data)
                text = data.decode("ascii", "replace") if printable else ""
                self.messages.append(f"{self.now():8.2f} RX {ftype:02x} {data.hex()} {text}")

    # ---------- keyboard ----------
    def keys(self):
        if not msvcrt:
            return
        while msvcrt.kbhit():
            ch = msvcrt.getwch()
            if self.raw_mode:
                if ch in "\r\n":
                    self.raw_mode = False
                    try:
                        parts = self.raw_buf.split()
                        if self.raw_kind == "fuzz":
                            # "TYPE p1 p2 p3 ..." each p is a hex payload
                            ftype = int(parts[0], 16)
                            payloads = [bytes.fromhex(p) for p in parts[1:]]
                            if not payloads:
                                raise ValueError("need at least one payload")
                            self.start_seq(self.build_config_fuzz(ftype, payloads),
                                           f"fuzz {ftype:02x} x{len(payloads)}")
                        elif self.raw_kind == "boot":
                            # "1f=07d0 20=00ff" -> arm boot injection with overrides
                            ov = {}
                            for tok in parts:
                                k, v = tok.split("=")
                                ov[int(k, 16)] = bytes.fromhex(v)
                            self.arm_boot_inject(ov)
                        elif self.raw_kind == "flags":
                            # hold a flag byte until cleared (k). "" clears.
                            if not parts:
                                self.flag_override = None
                                self.messages.append("flag hold cleared")
                            else:
                                self.flag_override = int(parts[0], 16)
                                self.messages.append(f"holding flags 0x{self.flag_override:02x} (k to clear)")
                        else:
                            t, h = parts
                            self.extra.append((int(t, 16), bytes.fromhex(h)))
                            self.messages.append(f"queued raw {t} {h}")
                    except Exception as e:  # noqa: BLE001
                        self.messages.append(f"bad input: {e}")
                    self.raw_buf = ""
                    self.raw_kind = None
                elif ch == "\x1b":
                    self.raw_mode = False
                    self.raw_buf = ""
                    self.raw_kind = None
                else:
                    self.raw_buf += ch
                continue
            if ch == "w":
                self.hold_until = time.perf_counter() + HOLD_TIMEOUT
            elif ch in "1234567890":
                self.level = 255 if ch == "0" else int(ch) * 255 // 10
            elif ch == "+":
                self.level = min(255, self.level + 5)
            elif ch == "-":
                self.level = max(0, self.level - 5)
            elif ch in (" ", "b"):
                self.level = 0
                self.latch = False
                self.hold_until = 0
            elif ch == "l":
                self.latch = not self.latch
            elif ch == "h":
                self.headlight_mode = {"auto": "on", "on": "off", "off": "auto"}[self.headlight_mode]
            elif ch == "n":
                self.extra.append((0x1B, b"SN"))
            elif ch == "v":
                self.extra.append((0x1D, b"FW"))
            elif ch == "e":
                self.extra.append((0x1E, b"\x20\x10"))
            elif ch == "f":
                self.extra.append((0x1F, b"\x02\xF4"))
            elif ch == "g":
                self.extra.append((0x20, b"\x00\x87"))
            elif ch == "x":
                self.raw_mode = True
                self.raw_kind = "frame"
                self.raw_buf = ""
            elif ch == "F":
                self.start_seq(self.build_flag_scan(), "flag scan")
            elif ch == "z":
                self.raw_mode = True
                self.raw_kind = "flags"
                self.raw_buf = ""
                self.messages.append("hold flags: type a hex byte e.g. 0c (bit2), 28 (bit5), 88 (bit7); empty=clear")
            elif ch == "B":
                self.raw_mode = True
                self.raw_kind = "boot"
                self.raw_buf = ""
                self.messages.append("boot-inject: type overrides e.g.  1f=07d0 20=00ff  (then power-cycle controller)")
            elif ch == "c":
                self.raw_mode = True
                self.raw_kind = "fuzz"
                self.raw_buf = ""
                self.messages.append("fuzz: type  TYPE p1 p2 ...  e.g.  1f 02f4 0300 04b0 05dc")
            elif ch == "k":
                self.cancel_seq()
            elif ch == "p":
                self.paused = not self.paused
            elif ch == "q":
                raise KeyboardInterrupt

    # ---------- experiment sequences ----------
    # A sequence is a deque of steps. Each step:
    #   {"dur": seconds, "flags": int|None, "send": [(type,data),...], "note": str}
    # While a sequence runs, polling continues; the flag byte is overridden when
    # "flags" is set, and any "send" frames are queued once at step start.
    def start_seq(self, steps, label):
        if self.seq:
            self.messages.append("a sequence is already running; press k to cancel")
            return
        self.seq = collections.deque(steps)
        self.seq_label = label
        self.seq_next = time.perf_counter()   # advance immediately to first step
        self.rawlog.write(f"{self.now():10.4f} SEQ-START {label}\n")
        self.messages.append(f"sequence started: {label}")

    def cancel_seq(self):
        if self.seq is not None:
            self.rawlog.write(f"{self.now():10.4f} SEQ-CANCEL {self.seq_label}\n")
            self.messages.append(f"sequence cancelled: {self.seq_label}")
        self.seq = None
        self.flag_override = None

    def seq_tick(self, now):
        if self.seq is None or now < self.seq_next:
            return
        # log the status snapshot captured at the END of the step just finished
        if self.status:
            s = self.status
            self.rawlog.write(f"{self.now():10.4f} SEQ-SNAP spd={s['speed_raw']} "
                              f"I={s['current_A']:.2f} v1={s['v1_raw']} v2={s['v2_raw']} "
                              f"b13={s['b13']:02x} b19={s['b19']:02x} b20={s['b20']} "
                              f"soc={s['soc_pct']}\n")
        if not self.seq:
            self.messages.append(f"sequence done: {self.seq_label}")
            self.rawlog.write(f"{self.now():10.4f} SEQ-END {self.seq_label}\n")
            self.seq = None
            self.flag_override = None
            return
        step = self.seq.popleft()
        self.seq_next = now + step.get("dur", 1.0)
        self.flag_override = step.get("flags")
        for t, d in step.get("send", []):
            self.extra.append((t, d))
        note = step.get("note", "")
        self.rawlog.write(f"{self.now():10.4f} SEQ-STEP {note} flags={self.flag_override} "
                          f"send={[f'{t:02x}:{d.hex()}' for t,d in step.get('send',[])]}\n")
        if note:
            self.messages.append(f"[{self.seq_label}] {note}")

    def build_flag_scan(self, dwell=3.0, bits=(0, 2, 5, 7)):
        """Sweep candidate poll flag bits. For each: dwell with it clear, then
        dwell with it set. Bit 1 (0x02) is excluded by default because it was
        found to CUT motor drive (coast), which would stop the wheel and spoil
        the rest of the sweep. Run at FULL throttle (press 0, l) and keep the
        wheel spinning so speed sits at 156 -- then a lifted limiter shows as
        speed climbing past 156 during a 'bit ON' phase."""
        base = 0x08
        steps = [{"dur": dwell, "flags": base,
                  "note": "baseline. Full throttle + spin; speed should sit ~156."}]
        for bit in bits:
            steps.append({"dur": dwell, "flags": base,
                          "note": f"bit {bit} (0x{1 << bit:02x}) OFF"})
            steps.append({"dur": dwell, "flags": base | (1 << bit),
                          "note": f"bit {bit} (0x{1 << bit:02x}) ON  <- watch speed cap / engagement"})
        steps.append({"dur": 1.0, "flags": base, "note": "flag scan complete"})
        return steps

    def on_ready(self):
        """Called when the controller's READY banner is seen on the raw stream."""
        self.log("RX", 0x1D, b"READY")
        self.messages.append(f"{self.now():8.2f} controller READY banner")
        if self.boot_inject is not None and self.boot_fire_at is None:
            self.boot_fire_at = time.perf_counter() + BOOT_INJECT_DELAY
            self.messages.append(f"READY seen -> injecting config in {BOOT_INJECT_DELAY:.1f}s")

    def arm_boot_inject(self, overrides):
        """Arm injection of the boot config frames after the next READY banner.
        `overrides` is {type:data}; types not given use the factory value.
        Polling keeps running so the tail light stays on and the controller
        sees normal bus activity while it boots."""
        self.boot_inject = overrides
        self.boot_fire_at = None
        show = " ".join(f"{t:02x}={d.hex()}" for t, d in overrides.items()) or "(factory)"
        self.rawlog.write(f"{self.now():10.4f} BOOT-ARM {show}\n")
        self.messages.append(f"boot-inject ARMED ({show}). Power-cycle the controller now; waiting for READY.")

    def fire_boot_inject(self):
        """Queue the boot handshake with any overridden config values."""
        ov = self.boot_inject or {}
        frames = [(0x1B, b"SN"), (0x1D, b"FW")]
        for t in (0x1E, 0x1F, 0x20):
            frames.append((t, ov.get(t, self.factory[t])))
        for t, d in frames:
            self.extra.append((t, d))
        show = " ".join(f"{t:02x}={d.hex()}" for t, d in frames)
        self.rawlog.write(f"{self.now():10.4f} BOOT-FIRE {show}\n")
        self.messages.append(f"boot config injected: {show}")
        self.boot_inject = None
        self.boot_fire_at = None

    def build_config_fuzz(self, ftype, payloads, dwell=1.5):
        """Send `ftype` with each payload in turn, dwell between, then restore
        the factory value for that type. Read-back replies (type-0x0A) are
        logged by pump()."""
        steps = []
        fac = self.factory.get(ftype)
        for p in payloads:
            steps.append({"dur": dwell, "send": [(ftype, p)],
                          "note": f"write {ftype:02x} <- {p.hex()}"})
        if fac is not None:
            steps.append({"dur": dwell, "send": [(ftype, fac)],
                          "note": f"RESTORE factory {ftype:02x} <- {fac.hex()}"})
        steps.append({"dur": 0.5, "note": "config fuzz complete"})
        return steps

    # ---------- display ----------
    def draw(self):
        st = self.status
        thr = self.throttle_now()
        age = (time.perf_counter() - self.last_status_t) if self.last_status_t else 999
        line = (f"thr {thr:3d}/{self.level:3d}{' LATCH' if self.latch else ''} "
                f"light {self.headlight_mode:4s}{'*' if self.headlight_on() else ' '} "
                f"{'PAUSED ' if self.paused else ''}")
        if st:
            line += (f"| spd {st['speed_raw']:3d} (~{st['speed_kmh_est']:4.1f} km/h) "
                     f"I {st['current_A']:4.1f}A chg {st['charge_A']:3.1f}A soc {st['soc_pct']:3d}% "
                     f"v1 {st['v1_raw']} v2 {st['v2_raw']} odo {st['odo_rev']} "
                     f"b10 {st['b10']:02x} b13 {st['b13']:02x} b19 {st['b19']:02x} b20 {st['b20']:3d} "
                     f"age {age:4.1f}s ")
        else:
            line += "| no status yet "
        line += f"| echo ok/bad {self.echo_ok}/{self.echo_bad} junk {self.junk}"
        if self.seq is not None:
            line += f" | SEQ {self.seq_label} ({len(self.seq)} left)"
        if self.flag_override is not None and self.seq is None:
            line += f" | FLAGHOLD 0x{self.flag_override:02x}"
        if self.boot_inject is not None:
            line += " | BOOT-ARMED waiting READY"
        if self.raw_mode:
            prompt = {"fuzz": "FUZZ", "flags": "FLAGS", "boot": "BOOT"}.get(self.raw_kind, "RAW")
            line += f"  {prompt}> {self.raw_buf}"
        sys.stdout.write("\r" + line[:220].ljust(220))
        sys.stdout.flush()
        while self.messages:
            sys.stdout.write("\r" + self.messages.popleft().ljust(220) + "\n")

    # ---------- startup wiring check ----------
    def selfcheck(self):
        """Send one poll and see whether our own bytes come back on RX."""
        self.ser.reset_input_buffer()
        time.sleep(0.05)
        idle = self.ser.read(4096)
        f = build(0x1A, bytes([0, 0x08]))
        self.ser.write(f)
        time.sleep(0.05)
        got = self.ser.read(4096)
        print(f"selfcheck: idle bytes before TX: {idle.hex() or 'none'}")
        print(f"selfcheck: sent {f.hex()}, received {got.hex() or 'NOTHING'}")
        if not got:
            print("  -> RX sees nothing of our own TX. TX is not reaching the bus, or RX is not on the bus.\n"
                  "     Check: diode direction (stripe to TX), RX really on the data line, common GND,\n"
                  "     and that the line idles high (measure it: should be ~3.3 V or 5 V).")
        elif got == f:
            print("  -> echo is perfect: TX and RX are both on the bus. If the controller stays silent,\n"
                  "     it is not powered, asleep, or does not read our low level (try a Schottky diode).")
        else:
            print("  -> echo is corrupted: bus contention, wrong baud, or the diode is not blocking.")
        self.rawlog.write(f"{self.now():10.4f} SELFCHECK idle={idle.hex()} sent={f.hex()} got={got.hex()}\n")
        self.ser.reset_input_buffer()

    # ---------- main loop ----------
    def run(self):
        self.selfcheck()
        n = 0
        next_poll = time.perf_counter()
        next_draw = next_poll
        next_summary = next_poll + 1.0
        try:
            while True:
                if time.perf_counter() >= next_summary:
                    next_summary += 1.0
                    self.rawlog.write(f"{self.now():10.4f} SUMMARY echo_ok={self.echo_ok} echo_bad={self.echo_bad} "
                                      f"junk={self.junk} status={self.status_count}\n")
                now = time.perf_counter()
                if self.boot_fire_at is not None and now >= self.boot_fire_at:
                    self.fire_boot_inject()
                self.seq_tick(now)
                if now >= next_poll:
                    next_poll += POLL_PERIOD
                    if now - next_poll > 0.5:      # fell far behind, resync
                        next_poll = now + POLL_PERIOD
                    if not self.paused:
                        n += 1
                        base = self.flag_override if self.flag_override is not None else 0x08
                        flags = base
                        if n % STATUS_EVERY == 0:
                            flags |= 0x10          # always keep status flowing
                        if self.flag_override is None and self.headlight_on():
                            flags |= 0x40
                        self.send(0x1A, bytes([self.throttle_now(), flags]))
                        if self.extra and n % STATUS_EVERY != 0:
                            t, d = self.extra.popleft()
                            self.send(t, d)
                self.pump()
                self.keys()
                if now >= next_draw:
                    next_draw = now + 0.1
                    self.draw()
                rem = next_poll - time.perf_counter()
                if rem > 0.002:
                    time.sleep(rem - 0.0015)
        except KeyboardInterrupt:
            pass
        finally:
            sys.stdout.write("\nstopping: sending zero-throttle polls\n")
            for _ in range(10):
                self.ser.write(build(0x1A, bytes([0, 0x08])))
                time.sleep(POLL_PERIOD)
            self.ser.close()
            self.rawlog.close()
            self.csvf.close()


def main():
    ap = argparse.ArgumentParser(description="Scooter one-wire bus master")
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--boot", action="store_true",
                    help="send the SN/FW queries and the three one-shot boot frames at start, like the real board")
    args = ap.parse_args()
    if not msvcrt:
        print("keyboard control needs Windows (msvcrt); polling still runs with throttle 0")
    print(__doc__)
    Master(args.port, args.baud, args.boot).run()


if __name__ == "__main__":
    main()
