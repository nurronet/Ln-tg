# LNWS Milestone 0.7 QA Standard Checkmark

Adds selectable timing QA standards and a live pass indicator in the main data bar.

## QA standards

- Standard: +/- 20 s/day
- Enhanced: +/- 10 s/day
- Top: +/- 5 s/day
- Chrono: +/- 2 s/day

After the current position remains within the selected rate tolerance for 5 continuous seconds, the data bar shows a green checkmark after the position.

Example:

```text
25200 bph  pos Dial Up ✓
```

Before the 5-second pass is achieved, the data bar shows the selected rate tolerance:

```text
25200 bph  pos Dial Up +/-20s
```

## Files changed

- `src/output_panel.c`
- `src/ln_station.c`
- `src/ln_station.h`
- `src/ln_station_panel.c`
- `src/ln_station_panel.h`

## Build

```bash
make clean
./autogen.sh
./configure
make
```
