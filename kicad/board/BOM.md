# Scooter control board — BOM (LCSC / JLCPCB oriented)

Hand-solderable board: 0805 passives, SOD-123 zeners/TVS, SOT-23 MOSFET,
through-hole headers, one low-profile SMD buzzer, one 2 mm SMD Amphenol
connector. The bus is native 3.3 V open-drain UART off the XIAO (no buffer IC).
Modules (XIAO, OLED) plug into headers; IMU is on-board.

| Ref(s) | Qty | Part | Package | LCSC | Notes |
|--------|-----|------|---------|------|-------|
| J3 | 1 | Amphenol 98424-F52-12ALF | 2×6, 2.0 mm SMD | **C239063** | confirmed; mates the scooter harness |
| R1–R4 | 4 | 100 kΩ 1% | 0805 | **C17407** | throttle + brake dividers (100k halves the 5 V signal at ~25 µA/divider). Basic |
| R6 | 1 | 100 Ω 1% | 0805 | **C17408** | bus series/protection (D6+D7 tied → R6 → bus). 100 Ω keeps the driven LOW clean (~0.4 V). Basic |
| R7 | 1 | 470 kΩ 1% | 0805 | **C17709** | pack divider top. Basic |
| R8 | 1 | 33 kΩ 1% | 0805 | **C17633** | pack divider bottom (was 30k → 33k to stay Basic; 470k/33k = 2.75 V @ 42 V). Basic |
| R9 | 1 | 1 kΩ 1% | 0805 | **C17513** | buzzer MOSFET gate series (D9→gate). Basic |
| R10 | 1 | 1 MΩ 1% | 0805 | **C17514** | buzzer MOSFET gate pulldown. **1M (not 100k): D9=GPIO9 is an ESP32-C3 boot-strap pin; a 100k pulldown drags it to ~2.3 V < V_IH at reset → download mode. 1M loads it lightly so it stays high.** Basic |
| R11, R12 | 2 | 4.7 kΩ 1% | 0805 | **C17673** | I²C pull-ups on SDA/SCL to +3V3. **Keep populated** — the on-board IMU (U2) is a bare chip with none, and the XIAO's internal pull-ups (~45k) are too weak. If your OLED module also has pull-ups they end up in parallel (~2.4k), still fine. Basic |
| R13 | 1 | 100 kΩ 1% | 0805 | **C17407** | pull-up on RGB_DIN (D8) to +3V3 — D8=GPIO8 is a boot-strap pin that would otherwise float at reset. Basic |
| R14 | 1 | 100 kΩ 1% | 0805 | **C17407** | pull-up on IMU_INT (D0/GPIO2) to +3V3 — holds the strap pin high at boot while the IMU is still initializing (INT1 hi-Z). See the IMU wake-on-motion note below. Basic |
| C1–C5, C8, C9 | 7 | 100 nF 50 V X7R | 0805 | **C49678** | ADC filters + decoupling (C8/C9 = IMU VDD/VDDIO). Basic |
| C11 | 1 | 100 nF 50 V X7R | 0805 | **C49678** | WS2812 local decoupling (+5V→GND at LED1, datasheet-recommended). Basic |
| C6, C7, C10 | 3 | 10 µF 16 V X5R | 0805 | **C1713** | C6 = 5 V bulk at Amphenol entry, C7 = 3V3 bulk, C10 = local buzzer decoupling (+5V→GND at BZ1, keeps the 2.7 kHz switching loop local). 16 V = >3× derating on the 5 V rail. Samsung CL21A106KOQNNNE, MOQ 20 |
| D1–D3 | 3 | 3.3 V Zener BZT52C3V3 | SOD-123 | **C173413** | clamp on throttle/brake/pack ADCs. Extended (no Basic in SOD-123) |
| D4 | 1 | SMF3.3A TVS (3.3 V, unidir.) | SOD-123 | **C2917875** | bus ESD/surge clamp (cathode→bus, anode→GND). ZHIDE SMF3.3A, MOQ 5 (was C283866, out of stock). Extended |
| Q1 | 1 | 2N7002 N-MOSFET (CJ) | SOT-23 | **C8545** | low-side buzzer switch (G/S/D = pad 1/2/3). Basic |
| LED1 | 1 | WS2812B-B/W (addressable RGB) | PLCC-4 5050 | **C114586** | status LED. **Powered at +5 V** (datasheet VDD 3.7–5.3 V, so 3.3 V is out of spec). The B/W variant's DIN V_IH is a fixed 2.7 V, so the ESP32's 3.3 V drives it directly — **no level shifter needed.** Extended |
| BZ1 | 1 | HYG-8503A-5027 buzzer, 5 V, passive | 8.5×8.5 mm SMD, **3.0 mm H** | **C18623826** | low-side driven by Q1; +→+5V (pad1), −→drain (pad3), pads 2/4 = fixing legs (NC). Firmware drives D9 with a ~2.7 kHz square wave. No flyback (per datasheet driving circuit). Extended |
| U2 | 1 | LSM6DS3TR-C IMU (accel + gyro) | LGA-14 2.5×3 mm | **C967633** | on-board IMU, I²C (0x6A), wake-on-motion INT1→D0. CS→3V3, SA0/SDx/SCx→GND. **Leadless — needs hot air/reflow or JLC assembly.** Extended |
| J4 | 1 | JST-GH 4-pin, right-angle SMD (SM04B-GHS-TB) | 1.25 mm, locking | **C189895** | off-board OLED link (I²C). Pinout **1=GND, 2=+3V3, 3=SCL, 4=SDA**. Locking latch = good for vibration. OLED now mounts to the enclosure, wired here via a GH cable. |
| U3 | 1 | Seeed XIAO ESP32-C3 module | LCC-14 (castellated, SMD) | **C19189385** | the MCU. **Official footprint = SMD/castellated direct-solder** (module reflows/drag-solders flat; no sockets). 14 edge pads (D0–D10, 3V3, GND, 5V) + battery/debug pads (unused). ⚠️ if you want it **socketed/removable**, ask for a through-hole footprint at the same 17×2.54 mm spacing instead. |
| J6 | 1 | 1×7 male header 2.54 mm | TH | **C2337** | expansion: D10, SDA, SCL, +5V, +3V3, GND, **PACK_41V** (pin 7, raw pack — GND on pin 6 buffers it from the 3V3 pins; expansion board handles its own protection). 1×40 breakaway, snap to length |

Off-board / sourced separately:
- **XIAO ESP32-C3** (Seeed) — now soldered as U3 (on-board), not a plug-in.
- **OLED 2.42″ SSD1309 128×64 I2C** (HS, on LCSC): white **C7466001** or blue **C7466000**. 4-pin TH 2.54 mm, pinout GND/VCC/SCL/SDA. **Now enclosure-mounted**, connected to J4 via a JST-GH cable — put a matching GH connector on the OLED's little adapter board, same pin order.
- **GH cable + OLED-side connector:** JST-GH 4-pin cable = **GHR-04V-S** housing (×2, one per end) + **SSHL-002T-P0.2** crimp contacts, or a pre-made 4-pin GH-to-GH cable. Wire it **straight-through 1:1** (GND/3V3/SCL/SDA). Keep it short (≲20 cm) for clean I²C; if long/flaky, drop R11/R12 to ~2.2 kΩ or run I²C at 100 kHz.

(The IMU is now the on-board **U2 LSM6DS3TR-C** chip — no longer a plug-in module.)

## Ordering / LCSC linkage

Every placed component now carries an **`LCSC`** schematic field with the part
number above, so the KiCad project itself is the source of truth (not just this
file). Generate a JLCPCB-ready BOM + placement file with the **Fabrication
Toolkit** plugin (bennymeg) or **File → Export → BOM** — point the tool's part
field at `LCSC`. J1/J2 are intentionally blank (your socket/mounting choice).

3D models: Q1, BZ1 and the Amphenol link portable `${KIPRJMOD}`-relative `.wrl`
models. The OLED (C7466001) has no vendor 3D model on EasyEDA, so J4 shows no
body in the 3D viewer — cosmetic only.

## Net / GPIO map

- **D0 IMU INT (deep-sleep wake) · D1 brake ADC · D2 throttle ADC · D3 pack ADC**
  - *(IMU_INT is on D0/GPIO2 — a boot-strap pin — deliberately: it's a clean digital line that a pull-up (R14) holds high at boot with no coupling to any rail. Pack was moved OFF the strap pin to D3/GPIO5, so its divider goes straight to GND with no rail coupling. See the two notes below.)*
  - *(pack is now on **ADC2** (D3/GPIO5). ADC2 on the C3 has a WiFi-coexistence caveat — reads can be blocked while the WiFi driver calibrates — but pack V is a secondary/coarse, retryable reading (bus SoC is primary), so it's fine. Throttle/brake stay on ADC1 (D2/D1).)*
  - *(all 3 strap pins are handled: D0=GPIO2 held high by R14; D8=GPIO8 by R13; D9=GPIO9 by R10=1M.)*
  - *(firmware ADC channels: brake = A1/D1, throttle = A2/D2, pack = A3/D3.)*

## IMU wake-on-motion + boot-strap (D0/GPIO2)

The IMU (U2) INT1 drives D0/GPIO2, which is both a **deep-sleep wake pin** (GPIO0–5 can wake the C3) and a **boot-strap pin**. Configure INT1 as **open-drain, active-low**, with **R14 (100 k)** pulling the line to +3V3:

- **Deep-sleep idle (no motion):** INT1 hi-Z → R14 holds the line high → *near-zero* sleep current (nothing conducts). Wake is armed on the LOW level.
- **Motion → wake:** INT1 pulls low → wakes the XIAO. On wake, firmware reads the IMU status reg to clear/deassert INT1.
- **Cold power-on:** the IMU takes ~15 ms to boot (INT1 hi-Z) while the ESP32 samples straps within ~1–3 ms → R14 holds D0 high → strap-safe.
- **At the wake instant** D0 is momentarily low, but only **GPIO9** decides run-app vs. download boot (it's untouched, internal pull-up), so the chip always boots the app; a low D0 at most affects JTAG (unused).

Firmware: `CTRL3_C` → PP_OD=1 (open-drain), H_LACTIVE=1 (active-low); route wake-up/activity IRQ to INT1; `esp_deep_sleep_enable_gpio_wakeup(BIT(2), ESP_GPIO_WAKEUP_GPIO_LOW)`. Bench-verify: sleep → tap → wakes & runs app, ~10×.

## Pack ADC (ADC2) vs. WiFi — firmware constraint

Pack sense is on **D3/GPIO5 = ADC2**. On the ESP32-C3 the WiFi driver owns ADC2 while it's running, so `adc2_get_raw()` fails/blocks **whenever WiFi is up** (not intermittent — unavailable while up, fully available while down). This is fine because the two are mutually exclusive by design:

- **Riding (normal): WiFi off** → pack ADC2 reads normally.
- **Config / OTA mode (entered by a throttle+brake gesture): WiFi on** → ADC2 blocked, but pack voltage isn't needed then, and the **controller's SoC over the bus is still available** (bus is unaffected by WiFi).

Key point: the **gesture trigger and drive-by-wire are on ADC1 (throttle D2 / brake D1)**, which WiFi never touches — so entering/exiting config mode is always reliable regardless of radio state.

Firmware rule: **read ADC2 (pack) only while the radio is stopped**; when WiFi is up, skip the pack read and fall back to the cached value or bus SoC. On exiting config mode, stop WiFi → ADC2 is available again immediately.

⚠️ If you ever run a **continuous radio during riding** (e.g. a BLE telemetry app), the radio is up while you want live pack voltage. BLE's ADC2 impact is less clear-cut than WiFi's — so in that case, use bus SoC for battery display while the radio is active and only sample ADC2 pack when it's idle. No hardware change either way.

- D4/D5 I2C (OLED + IMU) · **D6/D7 one-wire bus — tied together, D6 = open-drain TX, D7 = RX, through R6 (100 Ω) to the bus; SMF3.3A TVS on the bus**
- D8 WS2812 data · D9 buzzer · D10 expansion (J6 also carries I²C, +5V, +3V3, GND and raw PACK_41V)
- Unlock = brake + throttle gesture in firmware (no button)
- **Bus is native 3.3 V open-drain half-duplex UART** — no level shifter/buffer. Firmware: set the D6 UART TX pad to open-drain, handle self-echo. The controller's ~740 Ω pull-up provides the highs; levels are a clean 0 ↔ 3.3 V.

## Amphenol J3 pin map (CORRECTED — connector is 180° rotated from first pass)

The original assignment was mirror-imaged; each signal moved from pin N to pin
13−N. Corrected map below.

| Pin (Odd/Even) | Amphenol | Signal |
|---|---|---|
| 1 | B1 | +5V | 2 | A1 | NC |
| 3 | B2 | BRK_SIG | 4 | A2 | NC |
| 5 | B3 | GND | 6 | A3 | GND |
| 7 | B4 | GND | 8 | A4 | BUS_DATA |
| 9 | B5 | THR_SIG | 10 | A5 | PACK_41V |
| 11 | B6 | +5V | 12 | A6 | +5V |
