# Firmware backlog

Deferred ideas and improvements, roughly in priority order.

## Anti-theft: active braking while locked

Idea: if someone tries to ride the scooter while it's **locked**, electrically
apply the motor brake (active/regen braking) to stop them, and show an "angry"
lock on the OLED (e.g. a shaking/filled padlock centred, maybe with a buzzer
alarm).

Open question — the mechanism isn't confirmed. PROTOCOL.md §6.3 says there is
**no brake field**; braking is done by clamping the throttle byte to 0, and the
unused poll-flag bits (0x01/0x02/0x04/0x20/0x80) were tested as "no observable
effect." The user recalls a bit that electrically engaged the brake, so:
- Re-investigate the poll flag byte (and any one-shot messages) for a bit that
  triggers regen/active braking on a free-spinning wheel — capture on the bench.
- If found: on an unlock-gesture-less ride attempt while locked (throttle raised
  + wheel turning while `security_is_locked()`), assert that bit, raise the
  motion alarm, and draw the angry-lock screen (new icon in icons.h).
- If not found: fall back to the existing throttle-inhibit (already safe) plus
  the alarm + angry-lock screen as a deterrent.

## OTA anti-brick rollback

The web OTA (`netcfg.c` `h_ota`, `POST /api/ota`) A/B-swaps slots and reboots,
so a *failed upload* can't brick the board (boot partition isn't switched). But a
flashed image that **boots yet is broken** is not caught: there's no rollback
validation, so the bad slot stays selected across resets.

Wire up the IDF rollback mechanism:
- Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (new image boots as
  `PENDING_VERIFY`).
- Early in a healthy boot (e.g. after the bus link + core init prove out), call
  `esp_ota_mark_app_valid_cancel_rollback()`.
- If the new image never reaches that point (crash/boot loop), the bootloader
  auto-reverts to the previous working slot.
Consider a short "confirming firmware…" state on the OLED before marking valid.

## Temperature calibration (raw -> °C)

The status frame carries two raw temperature fields (`temp1` = controller/FET,
`temp2` = motor?) that we currently pass through unscaled. Work out the raw->°C
mapping (likely an NTC curve or a linear fit) and expose the temperatures in
real units on the OLED / console / web UI, with a small calibration step if the
curve isn't known from the datasheet. See PROTOCOL.md (temp / `b20` fields).

## Done / superseded

- ~~Adaptive range estimate~~ — `range.c` is charge-based (coulomb counting):
  integrate pack current over distance while riding -> learned Ah/km (EMA),
  range = SoC% x pack_capacity_mah / Ah_per_km. Seeded from range_full_km,
  persisted to NVS, `range reset` to re-seed. `pack_capacity_mah` from the label
  (12.8 Ah). Follow-up: learn/trim capacity from an observed full discharge.
- ~~Pack voltage jitter while stationary~~ — added oversampling + EMA in
  `inputs_sample_pack()`.
- ~~OLED state string overlapping the footer~~ — resolved by the banded layout
  redesign (state is now an icon).
