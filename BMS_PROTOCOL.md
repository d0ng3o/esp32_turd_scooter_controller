# Battery BMS UART Protocol (Hanyi / ELITOP)

Transcribed from the pack maker's own protocol document ("BMS UART, English
version", rev 1.2) for the exact battery in the tested scooter:

- **Pack:** `HY-RDF-S1004UM-MH1` — 10S LG-MH1 cells, 37 V nominal / 42 V charge,
  12.8 Ah (473.6 Wh).
- **Maker:** 汇业电子 (Hanyi/Huiye Electronics) — YanCheng Elite New Energy /
  **ELITOP**, elitop.net.

This is a **separate bus from the motor-controller bus** in [PROTOCOL.md](PROTOCOL.md):
the BMS lives inside the pack and talks to the **motor controller**, which polls it
for state-of-charge and relays only SoC onward to the control board. The control
board's harness never touches this line — to read the BMS you tap the pack↔controller
data wire directly (see "Reading it from our board" below).

> ⚠️ **Not yet verified on hardware.** The hex frames, byte offsets, and field
> types below are transcribed from the vendor PDF (whose prose was mangled by an
> extra machine-translation pass; the hex and the field table survived intact and
> the `0x02` payload checks out to exactly 16 bytes). **Integer endianness, the
> sign convention of the current field, and one temperature-status bit are marked
> below as unconfirmed** — settle them against a live capture before trusting them.

---

## 1. Physical layer

| Property | Value |
|----------|-------|
| Interface | USART, **half-duplex, single wire**, 3.3–5 V logic |
| UART | **9600** baud, 8N1 |
| Mode | Query / response — the battery answers **only** when polled |
| Role | The **motor controller is the master**; the BMS is the slave |

(Contrast the motor-controller bus: 115200 baud, XOR checksum. Same colon-framed
*family*, different instance.)

---

## 2. Frame format (both directions)

```
  0x3A  ID  CMD  LEN  DATA[LEN]  SUM_lo  SUM_hi  0x0D  0x0A
   ':'  │   │    │    │          │                └ CR LF terminator
        │   │    │    │          └ 16-bit checksum, low byte first
        │   │    │    └ number of DATA bytes
        │   │    └ command / response code
        │   └ device ID = 0x16 (fixed for this pack)
        └ start of frame = ':'
```

- **Checksum** = 16-bit cumulative **sum** of `ID + CMD + LEN + every DATA byte`,
  transmitted **low byte first, high byte last**.
- A host query carries no data (`LEN = 0`), so its checksum is simply `ID + CMD`:
  - `0x02` → `0x18`  ·  `0x03` → `0x19`  ·  `0x04` → `0x1A`  ·  `0x05` → `0x1B`
- **Endianness of multi-byte DATA fields is unconfirmed.** The checksum is
  little-endian, so the fields are *probably* little-endian too — verify on a
  capture before relying on it.

---

## 3. Commands

All four documented commands are **reads**. There is **no write / sleep / MOS-control
command** — the host cannot tell this BMS to shut down or open its discharge MOS.
(That is why the pack self-discharge can't be fixed over this bus; see
[firmware/BACKLOG.md](firmware/BACKLOG.md) and PROTOCOL.md §6.5.)

| CMD | Reads | Host frame | Reply `LEN` |
|-----|-------|-----------|-------------|
| `0x02` | Pack summary (V, I, capacity, SoC, temps, status) | `3A 16 02 00 18 00 0D 0A` | `0x10` (16 B) |
| `0x03` | All cell voltages | `3A 16 03 00 19 00 0D 0A` | `0x1A` (26 B) |
| `0x04` | Serial number (ASCII) | `3A 16 04 00 1A 00 0D 0A` | `0x0E` (14 B) |
| `0x05` | Hardware + software version | `3A 16 05 00 1B 00 0D 0A` | `0x02` (2 B) |

### 3.1 `0x02` — pack summary (16-byte payload)

Byte offsets are 1-based, as the vendor table numbers them.

| Byte | Field | Type | Unit |
|------|-------|------|------|
| 1 | Work status | INT8U | bitfield (§4.1) |
| 2 | Battery status | INT8U | bitfield (§4.2) |
| 3 | Temperature status | INT8U | bitfield (§4.3) |
| 4 | Temperature 1 | INT8S | °C |
| 5 | Temperature 2 | INT8S | °C |
| 6–7 | Total voltage | INT16U | mV |
| 8–9 | Current | INT16S | mA (sign convention unconfirmed) |
| 10–11 | Remaining capacity | INT16U | mAh |
| 12–13 | Full capacity | INT16U | mAh |
| 14 | State of charge | INT8U | % |
| 15–16 | Cycle count | INT16U | — |

This is a superset of what the motor-controller bus forwards: PROTOCOL.md §8 notes
that bus sends **only SoC** — here we additionally get true pack voltage, signed
current, remaining/full mAh, cycle count, and two °C temperatures straight from the
battery.

### 3.2 `0x03` — cell voltages (26-byte payload)

13 × INT16U, millivolts, cell 1 first (bytes 1–2), cell 13 last (bytes 25–26). The
pack is 10S, so cells 11–13 are expected to read 0.

### 3.3 `0x04` — serial number (14-byte ASCII)

Example reply data: `42 31 38 33 32 2D 30 30 30 30 30 30 30 30` = `"B1832-00000000"`.
Naming rule `RF 01 L YY DDD XXXX`: `RF` = fixed customer code, `01` = customer
vehicle model, `L` = LG cells, `YY` = year, `DDD` = day-of-year, `XXXX` = serial.
(The tested pack's barcode begins `RF02P…`, matching the `RF` customer prefix.)

### 3.4 `0x05` — version (2-byte payload)

`byte1` = hardware version, `byte2` = software version, each packed as BCD-style
nibbles (e.g. `0x12` = v1.2, `0x35` = v3.5).

---

## 4. Status bitfields

### 4.1 Work status (byte 1)

| Bit | 1 = |
|-----|-----|
| 0 | Discharge MOS enabled (0 = off) |
| 1 | Charge MOS on (0 = off) |
| 2–5 | unused |
| 6 | Charger connected |
| 7 | Battery short-circuit state |

### 4.2 Battery status (byte 2)

| Bit | 1 = |
|-----|-----|
| 0 | Under-voltage protection |
| 1 | Low-voltage warning |
| 2 | Over-voltage protection |
| 3 | Over-voltage warning |
| 4 | Discharge over-current protection |
| 5 | Battery discharging |
| 6 | Charge over-current protection |
| 7 | Battery charging |

### 4.3 Temperature status (byte 3)

| Bit | 1 = |
|-----|-----|
| 0 | Low-temperature charge protection |
| 1 | Low-temperature charge warning |
| 2 | High-temperature charge protection *(garbled in source — unconfirmed)* |
| 3 | High-temperature charge warning |
| 4 | Low-temperature discharge protection |
| 5 | Low-temperature discharge warning |
| 6 | Over-temperature discharge protection |
| 7 | Over-temperature discharge warning |

---

## 5. Reading it from our board

The control board has **no wire to this line** (its harness only carries an analog
`PACK_41V` sense, not the BMS data pin). To add BMS telemetry:

1. Tap the pack↔controller **data wire** at the battery connector.
2. Run it to the spare **`D10` / GPIO10** expansion pin on the control board.
3. Add a **second UART at 9600 8N1**, open-drain like the main bus.
4. **Sniff, don't poll.** The motor controller is already the BMS master; adding a
   second master would collide (same hazard as the main bus). Listen passively to
   the controller's existing `0x02`/`0x03` polls and the BMS replies, and decode
   them read-only. (Only if the controller turns out *not* to poll continuously
   would we need to master the bus ourselves, during idle windows.)

Payoff: real pack voltage + per-cell voltages + true current + mAh + cycle count →
much better range estimation and genuine cell-health monitoring.
