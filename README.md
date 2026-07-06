# LN Timegrapher Station Overlay v0.1

Adds a lightweight LN station layer for the `Ln-tg` fork.

## What this overlay adds

- scan-friendly identity field
- operator field
- timing position selector
- JSON export scaffold
- future ERPNext submit API boundary
- no dependency on ERPNext at runtime
- no change to the signal-processing core

## Why this shape

The upstream app is GPL-2.0 and native GTK/C. This overlay keeps LN-specific station behavior isolated so the fork can still track upstream while adding workshop workflow features.

## Files

- `src/ln_station.h`
- `src/ln_station.c`
- `src/ln_station_panel.h`
- `src/ln_station_panel.c`
- `examples/timing_result_example.json`
- `patches/INTEGRATION_NOTES.md`
- `patches/Makefile.am.snippet`

## Next step

After these files compile in the fork, v0.2 should connect the export call to the place in the code where stable timing values are updated.
