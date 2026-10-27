# Firmware backlog

Deferred ideas and improvements, roughly in priority order.

## Anti-theft: active braking while locked

Idea: if someone tries to ride the scooter while it's **locked**, electrically
brake the motor to stop them, show an "angry" lock on the OLED (a shaking/filled
padlock centred), and sound the alarm.

Mechanism — now identified (PROTOCOL.md §3.1 / §6.3). The poll flag byte carries
two braking bits the stock board never used but the controller honors:
- **0x02** = cut drive / coast (motor stops propelling, wheel freewheels).
- **0x04** = active dynamic brake (drives the wheel to a stop). Dissipative, not
  regenerative — no charge back to the pack (`b7` stays 0); braking energy and
  heat land in the motor windings + controller FETs, and both rise with speed.

Security model:
- **Locked = the motor is never powered.** The throttle byte is held at 0 for
  the entire time the scooter is locked, no matter what the rider does — this is
  already enforced in `control_step()` (locked inhibits throttle). A thief gets
  no propulsion, period.
- **Only an unlock** (the brake+throttle unlock gesture) re-grants propulsion.
  Nothing else — not motion, not a power cycle — enables the motor.
- The active brake below is purely an **escalation** when someone tries to push
  or kick the scooter along while it's locked: it physically resists them. It is
  never involved in normal, unlocked riding.

Safe engagement sequence (avoids slamming a dynamic brake at speed, which is an
unmeasured thermal/stability risk):
1. Detect a locked ride attempt: `security_is_locked()` AND the wheel is turning
   (speed > 0) — i.e. it's being pushed/kicked while locked (throttle is already
   inhibited, so "raised throttle" is just an extra hint, not required).
2. Raise the motion alarm (buzzer) + draw the angry-lock screen (new icon in
   icons.h).
3. Assert **0x02** to cut drive (belt-and-suspenders; throttle is already 0).
4. Once speed < ~1 km/h (~6 speed-counts at 0.16 km/h/count), assert **0x04** to
   actively lock the wheel stopped. Hold it braked while the attempt continues;
   release when the wheel has been still for a few seconds. An unlock gesture
   always cancels the brake immediately and returns to normal ride control.

Before shipping: re-confirm 0x02/0x04 on the bench (wheel off the ground) and log
battery current + `temp1` while braking from a few speeds, to verify no pack
surge and acceptable FET heating — the bits were found in a single fuzz session
(2026-09-14) and the ride-time current/temperature during a 0x04 brake were never
logged. Keep the existing throttle-inhibit as the always-safe fallback if either
bit fails to reproduce.

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
