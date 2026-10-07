# XIAO ESP32-C3 scooter control board — KiCad

Replacement control board for a decommissioned **Bird One #590AE** e-scooter.
A **Seeed XIAO ESP32-C3** reads the throttle and brake, talks to the Lishui
motor controller over the scooter's native **one-wire 3.3 V open-drain UART**
(115200), senses pack voltage, drives a buzzer and an addressable status LED,
carries an on-board IMU for wake-on-motion, and shows telemetry on an
enclosure-mounted OLED.

Designed in **KiCad 10**.

## Layout

```
kicad/
├── board/
│   ├── scooter_board.kicad_pro   project
│   ├── scooter_board.kicad_sch   schematic  ─┐ source of truth
│   ├── scooter_board.kicad_pcb   PCB         ─┘
│   ├── BOM.md                    bill of materials + full design rationale
│   ├── LAYOUT.md                 placement/routing playbook
│   ├── fp-lib-table              points at ../lcsc (the project footprint lib)
│   └── fabrication-toolkit-options.json
├── lcsc/                         project-local parts (LCSC-sourced)
│   ├── scooter_lcsc.kicad_sym    symbols
│   ├── scooter_lcsc.pretty/      footprints (6, all used by the board)
│   └── scooter_lcsc.3dshapes/    3D models for those footprints
├── lcsc_order_bom.csv            LCSC BOM-Tool upload (5 boards, no spares)
└── poop_logo.svg                 silkscreen art
```

Open `board/scooter_board.kicad_pro` in KiCad 10. The project footprint/symbol
libraries under `lcsc/` are referenced project-relative (`${KIPRJMOD}`), so the
board opens with all parts and 3D models intact — nothing to install.

## The board at a glance

- **2-layer, ~61 × 61 mm**, GND pour both sides with stitching vias.
- **MCU:** XIAO ESP32-C3 (U3), direct-solder castellated footprint.
- **Bus:** native 3.3 V open-drain half-duplex UART — D6 (OD TX) + D7 (RX) tied,
  100 Ω series (R6) to the harness, SMF3.3A TVS (D4) at the port. **No buffer IC.**
- **Analog:** throttle / brake / pack dividers → ADCs, 3.3 V zener clamps (D1–D3).
  Pack sense is on ADC2 (WiFi-coexistence caveat — see BOM.md).
- **IMU:** on-board LSM6DS3TR-C (U2, LGA-14), I²C, INT1 → D0 for deep-sleep
  wake-on-motion.
- **Outputs:** passive buzzer (BZ1 + Q1 low-side), WS2812B status LED (LED1, +5 V).
- **Connectors:** Amphenol 98424-F52-12ALF harness (J3), JST-GH to the off-board
  OLED (J4), 1×7 expansion header (J6, carries raw PACK_41V on pin 7).

Full part list, LCSC numbers, GPIO map, Amphenol pinout and the reasoning behind
every choice are in **[board/BOM.md](board/BOM.md)**. Layout/placement guidance is
in **[board/LAYOUT.md](board/LAYOUT.md)**.

## Ordering

Every placed part carries an `LCSC` field, so the project is the source of truth
for sourcing. Two paths:

- **Bare boards + self-assembly (current plan):** export Gerbers/drill from the
  PCB editor and order the bare board; order the parts from LCSC by uploading
  `lcsc_order_bom.csv` to the [LCSC BOM Tool](https://www.lcsc.com/bom).
- **JLC assembly:** generate a BOM + CPL with the Fabrication Toolkit plugin
  (part field = `LCSC`). Note U2 (LGA) and J3 (2 mm SMD) need hot-air/assembly.

Run **DRC** before ordering. The board is DRC-clean as committed.
