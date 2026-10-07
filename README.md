# Scooter control board

A standalone replacement control board for a **decommissioned Bird One #590AE**
shared e-scooter (the operator went bankrupt), built around a **Seeed XIAO
ESP32-C3**.

> ⚠️ **Safety-critical, experimental, unofficial.** This board controls a
> vehicle's motor, throttle, and braking behavior. It is provided **as-is, with
> no warranty**, is not certified by anyone, and is not affiliated with or
> endorsed by Bird. Build and ride at your own risk — read
> **[DISCLAIMER.md](DISCLAIMER.md)** first.

The project has two halves:

1. **Reverse-engineering** the scooter's one-wire bus between the original
   control board and the Lishui motor controller — captured and documented so
   the controller can be driven directly.
2. A **custom KiCad control board** that replaces the dead original: same
   harness connector, same bus protocol, plus throttle/brake sensing, pack
   voltage, a buzzer, an addressable status LED, an on-board IMU for
   wake-on-motion, and an OLED display.

## Repository layout

```
.
├── PROTOCOL.md                 the reverse-engineered one-wire bus protocol
├── captures/                   raw bus captures backing PROTOCOL.md
├── esp32-scooter-schematic.html  interactive overview of the board
│
├── scooter_master.py           drive the motor controller as the bus master
├── pi_sniff.py                 passive ride logger (Raspberry Pi, RX-only)
├── bus_experiment.py           scripted bring-up probes
├── bus_listen_poll.py          poll at 50 Hz and log non-echo bytes
├── bus_soak.py                 long idle soak to catch slow telemetry fields
├── install_logger.sh           one-shot Pi logger installer
├── pi-scooter-logger.service   systemd unit for the Pi logger
│
└── kicad/                      the hardware — see kicad/README.md
```

## The bus, briefly

Native **3.3 V open-drain half-duplex UART at 115200**. The control board is the
master: it polls the controller at 50 Hz with a colon-framed packet
(`':' TYPE LEN DATA[LEN] CHK CR LF`, `CHK` = XOR of the header+data) carrying the
throttle and light flags, and the controller answers with telemetry. Full frame
types, flags and field map are in **[PROTOCOL.md](PROTOCOL.md)**; it was all
confirmed on hardware (the PC drove the motor across the full throttle range).

## The hardware

The KiCad 10 design lives in **[kicad/](kicad/)** — see
[kicad/README.md](kicad/README.md) for the board overview,
[kicad/board/BOM.md](kicad/board/BOM.md) for the parts and full design
rationale, and [kicad/board/LAYOUT.md](kicad/board/LAYOUT.md) for the layout
playbook. The board is DRC-clean; bare boards and parts have been ordered.

## Status

- ✅ Bus protocol reverse-engineered and documented.
- ✅ Host/Pi tooling to drive and log the bus.
- ✅ Control board schematic + PCB complete, DRC-clean, ordered.
- ⬜ ESP32-C3 firmware — not in this repo yet.

## Note

The Python here is **host-side** tooling (PC / Raspberry Pi) used to
reverse-engineer and exercise the bus. It connects to the scooter via a USB
serial adapter (`scooter_master.py`, bench scripts) or a Pi UART (`pi_sniff.py`).
The firmware that will run on the XIAO itself is separate and not committed yet.

## License

This project is **free/libre and strong-copyleft** — multi-licensed by component,
with each layer under a **share-alike** license. Anyone who copies, modifies, or
builds on it **must release their version under the same license** (no closed or
proprietary forks):

| Component | Files | License |
|---|---|---|
| Code / firmware / scripts | `*.py`, `*.sh`, `*.service` | **GPL-3.0-or-later** — [`LICENSE`](LICENSE) |
| Hardware design (original) | `kicad/board/`, original symbols/footprints | **CERN-OHL-S-2.0** — [`LICENSE-HARDWARE.txt`](LICENSE-HARDWARE.txt) |
| Documentation | `*.md`, BOMs, `esp32-scooter-schematic.html` | **CC-BY-SA-4.0** — [`LICENSE-DOCS.txt`](LICENSE-DOCS.txt) |

Copyright 2026 Taavi Laadung.

**Exception:** the vendor-sourced symbols, footprints, and 3D models under
`kicad/lcsc/` are **not** covered by the above and keep their original
LCSC/EasyEDA/manufacturer terms — see [`kicad/lcsc/README.md`](kicad/lcsc/README.md).

**CERN-OHL-S v2 notice (hardware):** *Copyright 2026 Taavi Laadung. This source
describes Open Hardware and is licensed under the CERN-OHL-S v2. You may
redistribute and modify this source and make products using it under the terms of
the CERN-OHL-S v2 (https://ohwr.org/cern_ohl_s_v2.txt). This source is distributed
WITHOUT ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING OF MERCHANTABILITY,
SATISFACTORY QUALITY AND FITNESS FOR A PARTICULAR PURPOSE. Please see the
CERN-OHL-S v2 for applicable conditions. Source location:*
`https://github.com/d0ng3o/esp32_turd_scooter_controller` *(CERN-OHL-S requires the
source location to be conveyed and, where practicable, kept visible on the product
itself).*

## Disclaimer

See **[DISCLAIMER.md](DISCLAIMER.md)** for the full safety warning and the
"no affiliation" / reverse-engineering notices. In short: this is an
experimental, uncertified board that controls a vehicle — **build and ride at
your own risk**, and check your local road-traffic laws before using it.
