# Scooter controller firmware (ESP-IDF)

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
| Ride state machine + interlocks (kick-to-start, brake→0, link-loss failsafe) | ✅ |
| Ride-feel shaping (throttle cap, curve, soft-start) | ✅ |
| NVS config store (all tunables, web-UI-ready) | ✅ |
| Configurable lights (headlight auto/on/off/manual, experimental brake strobe) | ✅ |
| Gestures: unlock (brake+triple-blip) / config-mode (brake+full 20 s) | ✅ |
| Lock/security: boots locked, auto-lock, motion alarm | ✅ |
| IMU (LSM6DS3TR-C) wake-on-motion + idle keep-alive | ✅ |
| Deep sleep to conserve the pack (two-stage idle; lights cleared before sleep) | ✅ |
| WiFi config portal + web UI + OTA | ⬜ next (config-mode entry is stubbed) |
| OLED (LVGL) · pack-voltage ADC · runtime NVS calibration routine | ⬜ later |

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

## Calibration (placeholder)

Throttle/brake endpoints in `board.h` (`*_RAW_MIN/MAX`) are placeholders until the
real harness is on the bench. A runtime calibration routine + NVS storage is a
planned later phase; for now, measure the released/full raw ADC values from the
monitor and set them in `board.h`.
