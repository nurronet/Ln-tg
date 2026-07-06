# LN TG v0.2.1 Modified Files Overlay

This zip contains modified/new files only. No patch script.

## Copy these files into the repo root

- `Makefile.am`
- `src/ln_station.c`
- `src/ln_station.h`
- `src/ln_station_panel.c`
- `src/ln_station_panel.h`

## Manual edit required

Because `src/interface.c` in this fork is compact/minified, I did not include a blind full-file replacement.

Open:

`src/interface_ln_station_additions.c`

and paste the marked blocks into your existing `src/interface.c`.

## Build

```bash
./autogen.sh
./configure
make
```
