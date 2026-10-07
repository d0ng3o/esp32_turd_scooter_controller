# Scooter controller firmware (ESP-IDF)

[![firmware build](https://github.com/d0ng3o/esp32_turd_scooter_controller/actions/workflows/firmware.yml/badge.svg)](https://github.com/d0ng3o/esp32_turd_scooter_controller/actions/workflows/firmware.yml)

Firmware for the on-board **Seeed XIAO ESP32-C3 (U3)**. It reads the rider's
throttle and brake, drives the Lishui motor controller over the scooter's
one-wire bus (see [`../PROTOCOL.md`](../PROTOCOL.md)), and enforces the safety
interlocks. Built with **ESP-IDF v5.2+**.

> ⚠️ Safety-critical. Read [`../DISCLAIMER.md`](../DISCLAIMER.md). The default
> state is always **throttle 0**; any lost bus link or engaged brake forces it.

## Status

| Area | State |
|---|---|
| One-wire open-drain UART bus (colon protocol, echo-filtered, 50 Hz poll + status decode) | ✅ |
| Throttle/brake ADC, filtering, mapping | ✅ (placeholder calibration) |
| Ride state machine + failsafes (brake→0, link-loss, lock) | ✅ |
| Kick-to-start | enforced by the motor controller (PROTOCOL.md §6.2) — not duplicated in firmware |
| Ride-feel shaping (throttle cap, curve, soft-start) | ✅ |
| NVS config store (all tunables, web-UI-ready) | ✅ |
| Configurable lights (headlight auto/on/off/manual, experimental brake strobe) | ✅ |
| Gestures: unlock (brake+triple-blip) / config-mode (brake+full 20 s) | ✅ |
| Lock/security: boots locked, auto-lock, motion alarm | ✅ |
| IMU (LSM6DS3TR-C) wake-on-motion + idle keep-alive | ✅ |
| Deep sleep to conserve the pack (two-stage idle; lights cleared before sleep) | ✅ |
| WiFi config portal + web UI + OTA (brake+20 s gesture) | ✅ |
| Pack-voltage sensing (ADC2, WiFi-gated) | ✅ |
| Guided throttle/brake calibration (web UI) | ✅ |
| OLED telemetry (SSD1309, LVGL) | ✅ |

**Feature-complete** against the plan. Remaining work is hardware validation
(the open-drain single-wire UART and the IMU register sequence want a scope).

## Layout

```
firmware/
├── CMakeLists.txt, sdkconfig.defaults, partitions.csv   # project + dual-OTA flash map
└── main/
    ├── board.h        pin map + tunables (edit here)
    ├── protocol.[ch]  frame build/decode + XOR checksum (ports PROTOCOL.md)
    ├── bus.[ch]       open-drain UART, echo filter, poll loop, telemetry
    ├── inputs.[ch]    throttle/brake ADC + mapping
    ├── control.[ch]   ride state machine + safety interlocks
    ├── buzzer.[ch]    LEDC tones
    ├── status_led.[ch] WS2812 (led_strip)
    └── app_main.c     init + real-time ride loop
```

## Building (manual, from scratch)

Built and verified with **ESP-IDF v5.4** (v5.2+ should work). The only external
dependency is the managed component `espressif/led_strip`, fetched automatically
on the first build.

### 1. Install ESP-IDF (one-time)

**Windows** (PowerShell):
```powershell
git clone -b release/v5.4 --recursive https://github.com/espressif/esp-idf.git "$env:USERPROFILE\esp\esp-idf"
& "$env:USERPROFILE\esp\esp-idf\install.ps1" esp32c3
```

**Linux / macOS**:
```bash
git clone -b release/v5.4 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32c3
```

(Alternatively use the official VS Code **ESP-IDF extension** or the Windows
installer — both wrap the same steps.)

### 2. Export the environment (each new shell)

This sets `IDF_PATH` and puts `idf.py` + the RISC-V toolchain on `PATH`. It does
**not** persist, so run it in every new terminal:

- **Windows** (PowerShell): `. "$env:USERPROFILE\esp\esp-idf\export.ps1"`
- **Linux / macOS**: `. ~/esp/esp-idf/export.sh`

### 3. Build

```bash
cd firmware
idf.py set-target esp32c3     # first time only (creates sdkconfig from sdkconfig.defaults)
idf.py build
```

The first build compiles all of ESP-IDF (several minutes); later builds are
incremental. Output: `build/turd_scooter_fw.bin` (~243 KB).

One-liner for a fresh Windows shell:
```powershell
. "$env:USERPROFILE\esp\esp-idf\export.ps1"; cd firmware; idf.py build
```

### 4. Flash & monitor

Connect the XIAO over USB-C (native USB-Serial-JTAG), then:
```bash
idf.py -p <PORT> flash monitor      # e.g. -p COM7 (Windows) or -p /dev/ttyACM0 (Linux)
```
Exit the monitor with **Ctrl-]**. Console/log runs on USB-Serial-JTAG, so
**UART1 (GPIO20/21) stays free for the motor bus** and flashing never fights the
bus link.

### Housekeeping

- `idf.py fullclean` — wipe `build/`.
- `idf.py menuconfig` — edit settings interactively (defaults come from
  `sdkconfig.defaults`; the generated `sdkconfig` and `build/` are git-ignored).
- `idf.py size` — flash/RAM usage breakdown.

## Pin map (XIAO silk → GPIO)

| Silk | GPIO | Function |
|---|---|---|
| D1 | 3 | brake (ADC1_CH3) |
| D2 | 4 | throttle (ADC1_CH4) |
| D6 | 21 | bus TX (open-drain) |
| D7 | 20 | bus RX (tied to D6) |
| D8 | 8 | WS2812 status LED |
| D9 | 9 | buzzer |

(D0 IMU-INT, D3 pack sense, D4/D5 I²C, D10 expansion are wired but used by later phases.)

## Bench testing (before the scooter arrives)

1. **Echo filter / loopback.** Jumper D6↔D7 (they're tied on the real board
   anyway). On boot the LED is red (no link) and you get the fault chirp — the
   poll stream transmits and its echo is stripped, so no frames mis-parse.
2. **Fake controller.** Drive the bus from a PC/USB-TTL running
   [`../scooter_master.py`](../scooter_master.py) as the *slave side*, or replay a
   `../captures/` status frame, and confirm the monitor shows the link going
   green and telemetry decoding (cross-check fields against `../pi_sniff.py`).
3. **Throttle/brake.** A pot on D2 (and D1) → watch the state machine: stopped =
   IDLE (blue), and with a simulated moving `speed>0` from the fake controller,
   release-then-apply throttle → RIDING (green); brake → BRAKING (amber), throttle 0.

## Configuration

Every tunable lives in the NVS config store and can be changed two ways, with no
rebuild — over the USB serial console, or the WiFi web portal. See the full
hardware bring-up steps in [BRINGUP.md](BRINGUP.md).

### USB serial console

Open the USB-Serial-JTAG port (`idf.py -p <PORT> monitor`, or any 115200 serial
terminal) and type at the `scooter>` prompt:

| Command | Description |
|---|---|
| `help` | list commands |
| `get` | print the full config as JSON |
| `set <key> <value>` | change one setting + save to NVS (e.g. `set throttle_cap 220`, `set headlight_mode 1`, `set ap_ssid myscooter`) |
| `status` | live telemetry: link / lock / speed / SoC / current / pack V |
| `cal start` … `cal apply` | guided calibration (`cal show` to watch, `cal cancel` to abort) |
| `wifi on` / `wifi off` | start/stop the config + OTA WiFi portal |
| `defaults` | reset config to defaults |
| `reboot` | restart |

Keys are the JSON field names printed by `get`.

### WiFi web portal (+ OTA)

Hold **brake + full throttle for 20 s** while stopped to bring up the SoftAP
(`ap_ssid`, default `turd-scooter`) at **192.168.4.1**. The page offers the same
settings as a form, a live status readout, the guided calibration, and **OTA**
firmware upload. It auto-exits after `config_timeout_s`.

### Calibration

Throttle/brake endpoints ship as placeholders in `board.h` until the real harness
is on the bench. Run `cal start` (console or web) → move both controls through
their full travel → `cal apply` to capture and save the real min/max.
