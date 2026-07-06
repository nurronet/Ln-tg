# LN Station v0.1 Integration Notes

This overlay intentionally avoids touching the DSP/timegrapher core.

## Add source files

Copy the files in `src/` into the repo's `src/` folder.

Then add these to the existing executable source list in `src/Makefile.am` or the root `Makefile.am`, depending on how the fork currently lists sources:

```make
src/ln_station.c
src/ln_station.h
src/ln_station_panel.c
src/ln_station_panel.h
```

Run:

```bash
./autogen.sh
./configure
make
```

## Add the panel to the UI

In the main GTK window construction code, include:

```c
#include "ln_station.h"
#include "ln_station_panel.h"
```

Create a context once:

```c
static LnStationContext ln_ctx;
ln_station_init(&ln_ctx);
```

Create the panel:

```c
GtkWidget *ln_panel = ln_station_panel_new(&ln_ctx);
```

Add `ln_panel` to the top bar/sidebar/grid where appropriate.

## Export a timing result

Where the app has stable calculated timing values, build:

```c
LnTimingResult result = {0};
result.rate_s_per_day = current_rate;
result.beat_error_ms = current_beat_error;
result.amplitude_deg = current_amplitude;
result.beat_frequency_bph = current_bph;
result.lift_angle_deg = current_lift_angle;
result.sample_rate_hz = current_sample_rate;
result.duration_seconds = measurement_duration;
snprintf(result.timestamp_iso, sizeof(result.timestamp_iso), "%s", timestamp);
snprintf(result.notes, sizeof(result.notes), "%s", "Saved from LN Station");

char out_path[1024];
ln_station_export_json(&ln_ctx, &result, out_path, sizeof(out_path));
```

v0.2 should add direct HTTP submission after we confirm where the stable measurement variables live.
