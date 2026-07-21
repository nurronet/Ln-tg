#ifndef LN_STATION_PANEL_H
#define LN_STATION_PANEL_H

#include <gtk/gtk.h>
#include "ln_station.h"

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget *ln_station_panel_new(LnStationContext *ctx);
void ln_station_panel_set_status(GtkWidget *panel, const char *status);
void ln_station_panel_advance_position(GtkWidget *panel);
void ln_station_panel_bind_actions(GtkWidget *panel, GCallback save_callback, GCallback next_callback, GCallback complete_callback, gpointer user_data);
void ln_station_panel_bind_sidebar_toggle(GtkWidget *panel, GCallback toggle_callback, gpointer user_data);

/* Pushes the current sample-capture window (see LnCaptureStatus) into the
 * MEASUREMENT card's Rate/Beat Error/Amplitude/BPH labels and the capture
 * button's label/sensitivity. Call once per refresh tick, after feeding
 * the latest reading to ln_station_capture_feed(). */
void ln_station_panel_refresh_measurement(GtkWidget *panel);

/* Wires the "Start Capture" / "Recapture" button to call
 * ln_station_capture_begin() and immediately refresh the display. */
void ln_station_panel_bind_capture(GtkWidget *panel);

#ifdef __cplusplus
}
#endif

#endif
