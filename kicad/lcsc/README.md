# Project parts library (vendor-sourced)

These symbols, footprints, and 3D models were imported from **LCSC / EasyEDA** and
the component manufacturers using [`easyeda2kicad`](https://github.com/uPesy/easyeda2kicad.py),
then adjusted for this board (e.g. the XIAO and Amphenol footprints).

## Licensing

Unlike the rest of this repository, the contents of this `lcsc/` directory are
**not original work** and are **not** covered by the project's CERN-OHL-S /
GPL-3.0 / CC-BY-SA licenses. They remain subject to the terms of their original
sources (LCSC / EasyEDA and the respective component manufacturers). They are
included only for convenience, so the board opens with parts and 3D bodies intact.

If you would rather not redistribute vendor models, delete the `.step` / `.wrl`
files (and, if you like, the whole library) and regenerate them from the LCSC part
numbers listed in [`../board/BOM.md`](../board/BOM.md):

```
python -m easyeda2kicad --full --lcsc_id C<number> --output scooter_lcsc
```

## Contents

- `scooter_lcsc.kicad_sym` — schematic symbols
- `scooter_lcsc.pretty/` — footprints (6, all used by the board)
- `scooter_lcsc.3dshapes/` — 3D models (`.step` + `.wrl`) for those footprints

The board also uses a few footprints from KiCad's **standard** libraries (JST-GH,
pin header) — those ship with KiCad and are not duplicated here.
