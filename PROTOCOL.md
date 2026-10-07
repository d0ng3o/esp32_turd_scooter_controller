# Scooter One-Wire Bus Protocol

Reverse-engineered 2026-09-11 from a decommissioned shared e-scooter
(manufacturer bankrupt). This document is the reference for writing software or
firmware that replaces the original **control board** and drives the **motor
controller** directly.

- **Master** = control board (sends polls, throttle, light commands, queries)
- **Slave** = motor controller (answers polls with telemetry, holds the motor)

Everything here was confirmed on hardware: the PC drove the motor across the
full throttle range through this protocol.

---

## 1. Physical layer

| Property        | Value                                              |
|-----------------|----------------------------------------------------|
| Bus             | Single wire, half-duplex, shared by both ends      |
| Idle level      | High, pulled up by the controller through ~740 Ω   |
| Logic           | 3.3 V TTL                                           |
| UART            | 115200 baud, 8 data bits, no parity, 1 stop (8N1)  |
| Direction       | Both ends transmit on the same wire; no flow ctrl  |

Because it is one wire, **every byte you transmit is also received back as an
echo.** Filter your own echo before parsing replies.

### Wiring a USB-TTL adapter as master

```
  adapter GND ------------------------------ scooter GND
  adapter RX  ------------------------------ data line        (read direct)
  adapter TX  ----[ 100 Ω ]----------------- data line        (drive through R)
```

The controller's ~740 Ω pull-up is strong. A diode-OR from TX only reaches
~1.3 V low and the controller ignores it; 1 kΩ in series gives ~1.9 V, also
ignored. **100 Ω gives a valid low and works.** The resistor limits contention
current (~30 mA for the ~2 ms the controller is replying) but leaves ~15 % of
frames colliding. For a permanent build use a real open-drain driver
(74HC07 / 74LVC07, or an FTDI adapter with TX inverted in EEPROM plus one
MOSFET) so there is no contention.

**Disconnect the original control board from the data line before driving the
bus. Never run two masters at once.**

---

## 2. Frame format (both directions)

The steady-state protocol is a colon-delimited ASCII-framed binary protocol.

```
  +------+------+------+=================+------+------+------+
  | 0x3A | TYPE | LEN  |   DATA[LEN]     | CHK  | 0x0D | 0x0A |
  +------+------+------+=================+------+------+------+
    ':'   type   len      payload          xor    CR     LF

  CHK = XOR of every byte from 0x3A through the last DATA byte (inclusive).
        i.e. XOR of  ':', TYPE, LEN, DATA[0..LEN-1].
  Total frame length = LEN + 6.
```

**Request / reply pairing:** the reply TYPE equals the request TYPE minus
`0x0A`. Poll `0x1A` → status `0x10`; serial `0x1B` → `0x11`; firmware
`0x1D` → `0x13`; `0x1E` → `0x14`.

### Checksum reference (C)

```c
uint8_t xor_chk(uint8_t type, const uint8_t *data, uint8_t len) {
    uint8_t c = 0x3A ^ type ^ len;
    for (uint8_t i = 0; i < len; i++) c ^= data[i];
    return c;
}
```

---

## 3. Master → Slave (control board → motor controller)

### 3.1 Poll `0x1A` — sent continuously at 50 Hz (every 20 ms)

This is the heartbeat. It carries throttle and light state, and every 5th poll
also requests a status reply. **The slave only stays responsive while polled.**

```
  :  1A  02  TT  FF  CK  CR LF
           |   |
           |   +-- FF = flags byte (see below)
           +------ TT = throttle 0..255  (SPEED setpoint, not torque)

  DATA[0] = throttle
  DATA[1] = flags:
        bit 3  0x08  tail light   (master keeps this set at all times)
        bit 4  0x10  request status reply this poll (set on every 5th poll)
        bit 6  0x40  headlight    (master sets it while the wheel is moving)
```

Flag byte values seen on the wire: `0x08` (normal), `0x18` (status request),
`0x48` (headlight on), `0x58` (headlight + status request).

Examples (throttle 0, idle):

```
  status-request poll :  3A 1A 02 00 18 3A 0D 0A
  normal poll         :  3A 1A 02 00 08 2A 0D 0A
```

**Cadence pattern:** 4 normal polls then 1 status-request poll, repeating →
50 polls/s, 10 status replies/s.

### 3.2 Serial-number query `0x1B` — DATA = `"SN"` (0x53 0x4E)

```
  :  1B  02  53 4E  3E  CR LF        ->  slave replies 0x11
```
Answered any time.

### 3.3 Firmware query `0x1D` — DATA = `"FW"` (0x46 0x57)

```
  :  1D  02  46 57  <ck>  CR LF      ->  slave replies 0x13
```
**Only answered once, shortly after the controller's own power-up.** Later
queries get no reply.

### 3.4 Boot one-shots (original board sent these once at startup)

| TYPE | DATA        | Reply        | Notes                                  |
|------|-------------|--------------|----------------------------------------|
| 0x1E | `20 10`     | `0x14 20 10` | echoed back                            |
| 0x1F | `02 F4`     | none         | no observable effect                   |
| 0x20 | `00 87`     | none         | no observable effect                   |

These appear **unnecessary** to drive the motor; the controller runs on a bare
poll stream. Kept here for completeness.

---

## 4. Slave → Master (motor controller → control board)

### 4.1 Boot banner (unsolicited)

About 5 s after the controller powers up it sends, once:

```
  :  1D  "READY"  CR LF        =  3A 1D 52 45 41 44 59 0D 0A
```
(Note: this uses TYPE 0x1D as a one-off banner; it is not a reply to anything.)

### 4.2 Status `0x10` — 21-byte reply to a status-request poll, ~10 Hz

**This is the telemetry frame.** All 16-bit fields are **big-endian**.

```
  :  10  15  [ 21 data bytes ]  CK  CR LF

  offset  name          type    meaning
  ------  ------------  ------  --------------------------------------------
   0- 1   temp1         u16 BE  controller/FET TEMPERATURE (raw, scale TBD).
                                ~2960 cool; rises with ride time, holds at idle;
                                proven thermal by ride (corr 0.93 with time, not
                                current) + flat over a 1 h idle soak.
   2- 3   temp2         u16 BE  second TEMPERATURE (motor?). Same behaviour,
                                smaller rise (~2960->2980 over a ride).
   4- 5   current       u16 BE  battery current, 0.01 A units. 16-bit CONFIRMED:
                                reached ~20 A (b4=0x07) under real road load.
                                (idle ~0.03 A; ~20 A under load)
   6      (zero)        u8      always 0
   7      charge_A      u8      charge current, 0.01 A units (max 2.55 A)
                                (0 with charger unplugged; ~1.6 A CC tapering to
                                ~0.2 A CV while charging)
   8- 9   soc           u16 BE  state of charge, percent. b8 (high) always 0
                                in all data; effectively a single byte here.
  10      status_bit    u8      0 or 1; a status bit (meaning unresolved,
                                leans toward 'moving')
  11      (zero)        u8      always 0
  12      speed         u8      wheel speed reading (0..~156 powered cap;
                                seen up to 165 coasting/downhill past the limit)
  13      flag13        u8      constant 0x0E (unknown)
  14      (zero)        u8      always 0
  15-17   odometer      u24 BE  wheel-revolution counter (1 count ≈ 1 rev)
  18      state_flags   u8      motion-state bit flags (NOT constant):
                                0x01 base/ready (always set), +0x02 at standstill,
                                +0x04 at high speed near the limiter, 0x0b rare
  19      flag19        u8      constant 0x10 (unknown)
  20      margin        u8      load-sensitive voltage margin (NOT temperature).
                                ~105 idle-no-load; -33 when headlight on; sags
                                under motor load; slowly declines as pack depletes;
                                climbs while charging. Bus/back-EMF headroom.
```

Worked example (idle, off-charger):

```
  3A 10 15 0B A4 0B A4 00 04 00 00 00 64 00 00 00 0E 00 15 87 E7 01 10 63 56 0D 0A
           \___/ \___/ \___/ ^  ^  \___/ ^  ^  ^  ^  ^  \______/ ^  ^  ^  ck
            2980  2980  0.04A |  |   100 |  |  |  |  |   1411047  |  | 0x63
            v1    v2    curr  0 chg soc  1  0 spd 0e 0  odo rev   1 0x10
```

Both current fields (offsets 4-5 and 7) are in **0.01 A units**. Confirmed by
the charge field: it caps at 2.55 A as a single byte and read ~1.6 A tapering
to ~0.2 A on the charger, matching a ~2 A scooter charger; at a 0.1 A scale it
would imply an impossible ~16 A charger.

### 4.3 Serial reply `0x11` — DATA = ASCII identifiers

```
  "RF02P192150317 RF01L192001491"
```
Two space-separated IDs (controller and, likely, a second board).

### 4.4 Firmware reply `0x13` — DATA = ASCII version string

```
  "C2.12-P0.2/3.3-B2.0/2.2"
```
Sent only once after boot (see 3.3).

### 4.5 Echo reply `0x14` — DATA = `20 10`

Reply to `0x1E`.

---

## 5. Sequencing / timing

### 5.1 Cold boot (as the original board did it)

```
  t=0 s     controller powers up
  t≈+5 s    controller sends  : 1D "READY" CR LF
  t≈+16 s   board begins probing with legacy framings, unanswered:
              - Xiaomi M365  (header 55 AA, addr 0x20)
              - unknown      (header BE EF)
              - Ninebot ES   (header 5A A5, src 0x21/0x3E dst 0x20)
            board sends SN/FW queries and the 1E/1F/20 one-shots (once)
  then      board settles into the colon protocol: 50 Hz polls forever
```

The legacy-probe phase is the board hunting for a known controller before
falling back to this vendor's own colon protocol. **You do not need to
reproduce it.** A bare 50 Hz poll stream is accepted, including after the
controller has been running a while.

### 5.2 Steady state

```
  poll 20 ms apart, pattern:  N N N N S  N N N N S ...   (S = status request)
  → 50 polls/s, 10 status frames/s
  Reply latency: status frame arrives within ~1 poll of the request.
```

### 5.3 Robustness observed

- **Poll gap:** stop polling for seconds, resume — status resumes on the next
  status-request poll. No re-handshake, no watchdog reset seen.
- **Peer drop/reconnect:** the controller resumes replying within ~200 ms of
  hearing polls again. It does **not** re-run the boot banner or probes.
- **Single-frame glitches:** occasional lone status frames carry a garbled
  field but still pass the checksum. **Filter single-frame outliers** (e.g. the
  odometer jumping and snapping back) in your reader.

---

## 6. Control behavior (for the firmware author)

### 6.1 Throttle is a SPEED setpoint, linear

Measured map (free-spinning wheel, 222 mm, ~25 km/h limit):

```
  throttle:    76   102   127   153   178   204   229   255
  speed rdg:   54    68    82    97   111   126   140   156
  free-I(A):  0.4   0.5   0.6   0.8   0.8   0.9   1.1   1.1
```

(Free-spin battery current, 0.01 A units. Real riding load draws more.)

Fit: `speed ≈ 0.56 × throttle + 11`, saturating at 156 (the 25 km/h limiter)
at throttle 255. Because it is a speed target, a lightly loaded wheel holds a
steady speed rather than running away.

Speed reading → real speed: at reading 156 the wheel turns ~10 rev/s. With a
0.70 m circumference that is ~7 m/s ≈ **25 km/h**, so the speed byte is
≈ 0.16 km/h per count (hypothesis: tenths of mph).

Odometer: **1 count ≈ 1 wheel revolution ≈ 0.70 m.** Not persisted by the
controller alone — it reset to its session-start value after a power cycle, so
the original board likely wrote it back.

### 6.2 Kick-to-start (safety interlock)

The motor engages **only if both** are true:

1. the wheel is already turning (any low speed reading works), and
2. the throttle **rises from idle** *after* motion is established
   (~0.3 s of confirmed motion first).

Throttle applied while stationary, or applied before/at the instant of the
push, is ignored until you release and re-apply. Design your throttle logic to
release to 0 and re-assert once the wheel is moving.

### 6.3 Brake

The lever is analog on the scooter but the board sends **no brake field**.
Instead, when the brake crosses ~half travel the master forces the **throttle
byte to 0x00** for as long as it is held. Braking is therefore "command speed
0"; there is no separate brake signal to the controller and no brake-light
change on the bus (the tail light is simply always on via flag 0x08).

Replicate this: read your brake input and clamp the throttle byte to 0 above
threshold.

### 6.4 Headlight

Master sets poll flag **0x40** while the controller reports speed > 0, and
clears it ~0.5 s after the wheel stops. The tail light is the always-on
flag 0x08. Both are commands from master to slave.

### 6.5 Power / charger interlock

- The controller drives the motor **only with battery power present.** With the
  battery disconnected it can boot on charger standby but reports 0.00 A and will
  not drive.
- To **ride**, the charger must be **unplugged** (a connected charger did not
  block motion in normal operation, but after a full battery disconnect the unit
  would only boot with the charger in — a BMS quirk; unplug it once running).

---

## 7. Minimal master loop (pseudocode)

```
open serial: 115200 8N1, COM4
n = 0
every 20 ms:
    n += 1
    flags = 0x08
    if n % 5 == 0:      flags |= 0x10        # request status
    if wheel_moving:    flags |= 0x40        # headlight
    throttle = brake_active ? 0 : throttle_cmd
    send_frame(0x1A, [throttle, flags])

on receive, after stripping our own echo:
    parse frames ':' TYPE LEN DATA CHK CR LF
    verify CHK (XOR)
    if TYPE == 0x10 and LEN == 21:  decode_status(DATA)   # see §4.2
    if TYPE == 0x11:  serial string
    if TYPE == 0x13:  firmware string
```

A reference implementation is in `scooter_master.py` (interactive, with logging
and a wiring self-check). Raw and decoded captures are under `captures/` and
`logs/`.

---

## 8. Open items

| Item                        | Status                                          |
|-----------------------------|-------------------------------------------------|
| `temp1`/`temp2` scale       | Confirmed temperatures; raw→°C needs a thermometer |
| `b20` margin scale          | Load-sensitive voltage margin; scale unmapped   |
| Raw pack voltage            | NOT transmitted; only SoC is sent               |
| `flag13` (0x0E), `flag19` (0x10) | Constant; meaning unknown                  |
| Speed-byte real-world unit  | ≈0.16 km/h/count (hypothesis, tenths of mph)    |
| Boot one-shots 0x1F / 0x20  | No observed effect; likely unnecessary          |
| Odometer persistence        | Not saved by controller alone                   |

`temp1`/`temp2` are confirmed temperatures (ride isolates them from load); to
calibrate raw→°C, hold a thermometer on the controller/motor while logging.
Pack voltage is not on the bus, only SoC.
