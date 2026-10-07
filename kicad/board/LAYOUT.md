# Scooter control board — PCB layout notes

Things to remember when placing and routing `scooter_board.kicad_pcb`.
Schematic-level placement hints (decoupling caps, IMU_INT) are also written as
text notes on the sheet itself. This file is the broader layout playbook.

The board has **no regulator and no high-current path** (buzzer ~90 mA is the
peak load), so there are **no thermal concerns** — this is a signal-integrity
and mechanical layout, not a power layout.

---

## 1. The big one: XIAO antenna keep-out (matters for WiFi/BLE)

The XIAO ESP32-C3 has a **PCB chip antenna at the USB-C end** of the module.
The module sits on sockets *above* this carrier board, so **copper on the
carrier under/around the antenna will detune it** and wreck WiFi range — which
you need for the config/OTA mode.

- Leave a **ground/copper keep-out** on *all layers* under the antenna end of
  the XIAO (roughly the last ~10 mm at the USB-C side). No pour, no traces there.
- Orient the XIAO so its antenna end points **off the board edge / toward open
  space**, not into the middle of the ground pour or toward the Amphenol/harness.
- Keep the buzzer, WS2812 and bus traces away from the antenna zone too.

## 2. Ground & power

- Use a **solid ground pour/plane** (bottom layer is fine) as the reference —
  except the antenna keep-out above.
- **Bulk caps at their jobs:** C6 at the Amphenol **+5V entry**, C7 on the 3V3
  rail, C10 right at the buzzer (see §5).
- **Wide traces for the current-carrying rails:** +5V feeds the XIAO regulator,
  buzzer and WS2812 — give it a fat trace/pour from the Amphenol. +3V3 similar.
- One clean ground: tie the analog return (divider/ADC grounds) and the digital
  ground to the same plane; just keep the *buzzer's* pulsed return loop physically
  away from the analog area (see §5).

## 3. Analog (throttle / brake / pack ADCs)

These are the noise-sensitive nets — treat them gently.

- **Filter caps C1 (thr) / C2 (brk) / C3 (pack) go next to the XIAO ADC pins**,
  not next to the dividers — they buffer the ADC sample-and-hold, so they want to
  be at the pin end.
- Keep the ADC nodes (`THR_ADC`, `BRK_ADC`, `PACK_ADC`) **short and clear of the
  buzzer, WS2812 (`RGB_DIN`) and the bus** — these are ~30–50 kΩ-impedance lines
  and pick up noise easily.
- The clamp zeners **D1/D2/D3 sit on the ADC nodes** near the XIAO.
- ADC pin map (for silkscreen/sanity): **D1 = brake, D2 = throttle, D3 = pack**
  (pack is ADC2 — see BOM's WiFi note).

## 4. One-wire bus

- **Put the TVS (D4) right at the Amphenol bus pin (J3.8)** so it clamps ESD at
  the port, before the signal reaches R6 and the XIAO.
- R6 (100 Ω) sits between the XIAO D6/D7 tie-point and the bus.
- The bus is only 115 kbaud, so trace length isn't critical — but still keep it
  off the analog nets.

## 5. Buzzer (BZ1 + Q1 + C10) — keep the loop tight

The buzzer is the one **noisy switching load** (2.7 kHz pulses).

- Keep the current loop **BZ1 → Q1 (drain) → C10 → back to +5V** physically
  small, with short/fat traces. That contains the switching noise locally.
- Place this whole cluster **away from the analog section (§3)** and the IMU.
- BZ1 pads **2 & 4 are mechanical fixing legs** (no net) — solder them for
  mechanical strength, but they carry nothing.
- Q1 SOT-23: G=pad1, S=pad2, D=pad3.

## 6. IMU (U2, LSM6DS3TR-C, LGA-14)

- **Leadless LGA — needs hot-air/reflow or JLC assembly**, not a hand iron.
- **C8 (VDD) and C9 (VDDIO) go hard against U2's power pins** with short ground
  returns — this is standard MEMS decoupling and matters for stable readings.
- **`IMU_INT` route short and clear of BZ/RGB/bus edges** to avoid false wakes
  (it's a high-impedance wake line). R14's own position is non-critical.
- Orientation: any orientation wakes on motion, but if you ever use tilt/axes,
  note the chip's X/Y/Z on the silkscreen relative to the scooter.

## 7. WS2812 status LED (LED1)

- **Powered at +5 V** (not 3V3). **C11 (100 nF) hugs LED1's VDD/GND.**
- Place it where the light is actually **visible** through/near the enclosure.
- `RGB_DIN` from D8 — keep it away from the analog nets.

## 8. Connectors & mechanical

- **Amphenol J3** (2 mm SMD, C239063): the harness interface. Check pin-1 /
  orientation against the mating harness before routing. This is the mechanical
  anchor to the scooter — its position is dictated by the harness, place it first.
- **XIAO (U3):** now a **single official footprint** (Seeed XIAO ESP32-C3,
  C19189385) — the two rows are at the correct fixed 17 × 2.54 mm spacing by
  construction, so no manual row-spacing to get wrong. It's the **SMD/castellated
  direct-solder** footprint (module sits flat, drag-solder the edge pads); the
  battery/debug pads on the underside are unused. Leave the **USB-C end
  accessible** for flashing/recovery, and honor the antenna keep-out (§1).
  *(If you switch to a socketed/removable through-hole footprint later, the
  antenna keep-out and USB-access notes still apply.)*
- **OLED — now off-board (J4 = JST-GH right-angle):** the 72 × 43 mm module is
  no longer on this board (it mounts to the enclosure and connects via a JST-GH
  cable). J4 is just the small 1.25 mm right-angle connector — **place it near a
  board edge with the cable exit facing the screen**, and keep its I²C legs away
  from the motor bus. No more overhang to plan around — that space is freed.
- **J6 expansion (1×7):** carries **raw PACK_41V on pin 7** — keep that pin's
  copper away from the 3V3 pins (GND on pin 6 already buffers it); the expansion
  board is responsible for its own protection.

## 9. High-voltage clearance (the 41 V pack)

- `PACK_41V` appears at the Amphenol (J3.10), R7's top pad, and J6 pin 7. It's
  **low current** (µA through the 470 k divider) so no trace-width concern, but
  keep sane **clearance** (≥ ~0.5 mm, easy at these pitches) from the low-voltage
  nets. Don't run 41 V under the XIAO or the analog dividers.
- +5 V from the harness can carry real current (XIAO + buzzer + LED) — that's the
  rail to make wide, not the 41 V sense line.

## 10. Board outline (odd shape)

- You're drawing the outline to match the original scooter PCB. Overlay a photo/
  scan on a user layer (File → Import graphics, or a bitmap on a fab layer) and
  trace `Edge.Cuts` + mounting holes to it. Place J3 (Amphenol) and the mounting
  holes to the mechanical reference first, then everything else around them.

## 11. Before ordering — checklist

- Run **DRC** (clearances, unrouted, courtyard overlaps) → clean.
- Confirm **antenna keep-out** is honored on all layers.
- Confirm XIAO **row spacing** and **USB-C access**.
- Confirm Amphenol **orientation** vs the real harness.
- Generate a **JLCPCB BOM + CPL** (Fabrication Toolkit, part field = `LCSC`);
  remember U2 (LGA) and J3 (2 mm SMD) want assembly or hot-air, and J1/J2 sockets
  are your own mechanical choice (no LCSC # assigned).
- 3D-view check for the OLED overhang and any part collisions.
