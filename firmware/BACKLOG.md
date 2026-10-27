# Firmware backlog

Deferred ideas and improvements, roughly in priority order.

## Adaptive range estimate (consumption-based)

The current range readout is a naive linear estimate: `range_full_km × SoC%`.
It needs a configured full-range number we don't actually know, and it ignores
how the scooter is really being ridden.

Replace it with a learned estimate from live data:

- **Energy used:** integrate pack power over time — `Wh += pack_V × pack_A × dt`
  each loop. Pack current (`telem.current_cA`) and the calibrated pack voltage
  are both available.
- **Distance:** use the odometer delta (`telem.odometer`, ~1 count/rev → metres
  via the wheel circumference).
- **Average consumption:** maintain `Wh_per_km` over a trailing window (e.g. an
  EMA over the last few km) so it adapts to terrain/rider/temperature.
- **Remaining energy:** `SoC% × pack_capacity_Wh`. Pack capacity is roughly
  known (Bird One ≈ 36 V × 5.2 Ah ≈ 187 Wh) — make it a config value and let it
  self-correct as a full discharge is observed.
- **Range:** `remaining_Wh / Wh_per_km`.
- **Fallback:** until enough distance/energy has accumulated for a stable
  `Wh_per_km`, fall back to the linear estimate (or a sane default consumption).
- Persist the learned `Wh_per_km` (and maybe capacity) in NVS so it survives
  reboots. Smooth the displayed number so it doesn't jump.

Config to add: `pack_capacity_wh`, maybe `wheel_circumference_mm`. Keep
`range_full_km` only as the cold-start fallback, or drop it once this lands.

## Temperature calibration (raw -> °C)

The status frame carries two raw temperature fields (`temp1` = controller/FET,
`temp2` = motor?) that we currently pass through unscaled. Work out the raw->°C
mapping (likely an NTC curve or a linear fit) and expose the temperatures in
real units on the OLED / console / web UI, with a small calibration step if the
curve isn't known from the datasheet. See PROTOCOL.md (temp / `b20` fields).

## Done / superseded

- ~~Pack voltage jitter while stationary~~ — added oversampling + EMA in
  `inputs_sample_pack()`.
- ~~OLED state string overlapping the footer~~ — resolved by the banded layout
  redesign (state is now an icon).
