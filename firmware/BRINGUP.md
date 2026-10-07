# Hardware bring-up checklist

Step-by-step validation for a freshly assembled board. Work through it **in
order** — each step de-risks the next. Keep the wheel **off the ground** (on a
stand) until the very end.

> ⚠️ This controls a vehicle. Read [`../DISCLAIMER.md`](../DISCLAIMER.md). The
> firmware defaults to throttle 0; don't defeat the interlocks during bring-up.

Most checks use the USB serial console (`idf.py -p <PORT> monitor`, prompt
`scooter>`) — see [README.md](README.md#usb-serial-console).

## 0. Board power & flash
- [ ] Inspect the board; no solder bridges, correct part orientations (U2 IMU, J3 Amphenol).
- [ ] Power the XIAO over USB-C; flash: `idf.py -p <PORT> flash monitor`.
- [ ] Boot log shows config load, bus up, `ride loop started`; LED/buzzer boot chirp.
- [ ] `status` → `link=0 locked=1` (boots locked, no controller yet). Good baseline.

## 1. Status LED & buzzer
- [ ] LED shows **purple** (locked) at boot. `set led_brightness` changes it.
- [ ] Boot chirp heard. `set buzzer_enable false` silences feedback; re-enable after.

## 2. One-wire bus (the #1 risk — open-drain single wire)
- [ ] With a scope on D6/D7: confirm the 50 Hz poll frames and that **TX idles high**
      (open-drain + controller/divider pull-up), driven low cleanly (~0.4 V).
- [ ] Connect to the motor controller (original control board **disconnected** —
      never two masters). `status` → `link=1`, and `speed`/`soc`/`current` populate.
- [ ] Confirm the software echo-filter works: no garbage frames, telemetry stable
      (cross-check values against `../pi_sniff.py` on a separate logger if unsure).

## 3. Throttle & brake inputs
- [ ] `cal start` → sweep throttle fully, squeeze brake fully → `cal apply`.
- [ ] `status`/`get` reflect sane endpoints. Throttle maps 0→255 across travel.
- [ ] `brk_active_pct` threshold feels right (brake "engages" near half travel).

## 4. Ride interlocks (wheel on a stand!)
- [ ] Stationary + throttle → **no motion** (kick-to-start). Spin the wheel by hand,
      then release-and-apply throttle → motor engages. Brake → throttle forced 0.
- [ ] Yank the bus connector mid-"ride" → **fault chirp**, throttle inhibited (`link=0`).
- [ ] Soft-start feels smooth (tune `soft_start_ms`); cap via `throttle_cap`.

## 5. IMU (LSM6DS3TR-C)
- [ ] Boot log: `LSM6DS3TR-C up` (WHO_AM_I = 0x6A at I²C 0x6A). If "not found",
      check SA0→GND, SCL/SDA, and R11/R12 pull-ups.
- [ ] **Re-check the register sequence vs the datasheet** (`imu.c`) — wake bits are
      datasheet-derived, unverified on hardware.
- [ ] Idle keep-alive: at a simulated stop, small jostles keep it from auto-locking.

## 6. OLED (SSD1309 via JST-GH)
- [ ] Connect the OLED; boot log shows `OLED up` (the I²C probe detects 0x3C).
- [ ] Display shows speed / SoC / state / current / pack V. If garbled, the SSD1309
      may need a different init than the SSD1306 driver — note and adjust.

## 7. Lights (verify against the real controller)
- [ ] Headlight comes on while moving (`headlight_mode` = auto).
- [ ] **Brake strobe is experimental**: confirm toggling the tail flag (0x08) on
      brake actually blinks the tail. If it misbehaves, `set brakelight_mode 0` (steady).

## 8. Lock / security / power
- [ ] Unlock gesture: hold brake + blip throttle 3× (stopped) → unlock chirp, `locked=0`.
- [ ] Auto-lock after `auto_lock_timeout_s` idle; motion alarm when moved while locked.
- [ ] Deep sleep: idle past `auto_sleep_timeout_s` → lights clear, current drops to µA;
      a tap (IMU) wakes it, it reboots **locked**, polling resumes.

## 9. WiFi config portal + OTA
- [ ] `wifi on` (or the brake+20 s gesture) → join `ap_ssid` @ 192.168.4.1.
- [ ] Settings save; live status updates; **OTA** a new build and confirm it reboots
      into it (the spare OTA slot).

## 10. Pack voltage
- [ ] `status` pack V matches a meter on the pack (within divider tolerance). Tune the
      divider constants in `board.h` if off.

---

When all boxes are ticked, the board is ride-ready. Change settings any time via
the console or web portal — no reflash needed.
