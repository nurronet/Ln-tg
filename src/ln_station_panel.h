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

#ifdef __cplusplus
}
#endif

#endif
