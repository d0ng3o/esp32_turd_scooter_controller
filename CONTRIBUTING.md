# Contributing

Thanks for your interest! This is a personal/hobby project — a standalone
replacement controller for a decommissioned e-scooter. Issues and PRs are
welcome.

## Repo layout

| Path | What |
|---|---|
| `PROTOCOL.md`, `captures/`, `*.py` | reverse-engineered bus protocol + host tooling |
| `kicad/` | the KiCad 10 control board (see `kicad/README.md`) |
| `firmware/` | ESP32-C3 firmware (see `firmware/README.md`) |

## Firmware

- Build with **ESP-IDF v5.4** — see [`firmware/README.md`](firmware/README.md)
  for the full install/build/flash steps. CI builds every change under
  `firmware/` (`.github/workflows/firmware.yml`); please keep it green.
- Hardware changes go through [`firmware/BRINGUP.md`](firmware/BRINGUP.md).
- Match the surrounding C style (K&R-ish, 4-space indent, snake_case, SPDX header
  on new files). Keep modules small and single-purpose.

## Safety

This project controls a vehicle's motor and braking behaviour. Keep the
fail-safe defaults intact: **any fault, lock, or brake must force the throttle to
0.** Don't weaken the interlocks (kick-to-start, link-loss, brake clamp) without
a very good reason and a clear note. Test on a stand, wheel off the ground.

## Licensing

By contributing you agree your contributions are licensed under the project's
licenses (see the root `README.md`):

- code → **GPL-3.0-or-later**
- hardware → **CERN-OHL-S-2.0**
- docs → **CC-BY-SA-4.0**

New source files should carry an `SPDX-License-Identifier` header. The
vendor-sourced parts under `kicad/lcsc/` are excluded (see that folder's README).
