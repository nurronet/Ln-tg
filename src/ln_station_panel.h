#ifndef LN_STATION_PANEL_H
#define LN_STATION_PANEL_H

#include <gtk/gtk.h>
#include "ln_station.h"

#ifdef __cplusplus
extern "C" {
#endif

GtkWidget *ln_station_panel_new(LnStationContext *ctx);
void ln_station_panel_set_status(GtkWidget *panel, const char *status);

#ifdef __cplusplus
}
#endif

#endif
