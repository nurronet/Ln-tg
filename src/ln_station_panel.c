#include "ln_station_panel.h"

typedef struct {
    LnStationContext *ctx;
    GtkWidget *identity_entry;
    GtkWidget *operator_entry;
    GtkWidget *position_combo;
    GtkWidget *status_label;
} LnStationPanel;

static void on_identity_changed(GtkEditable *editable, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    ln_station_set_identity(panel->ctx, text);
}

static void on_operator_changed(GtkEditable *editable, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    ln_station_set_operator(panel->ctx, text);
}

static void on_position_changed(GtkComboBoxText *combo, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gchar *text = gtk_combo_box_text_get_active_text(combo);
    if (text) {
        ln_station_set_position(panel->ctx, text);
        g_free(text);
    }
}

GtkWidget *ln_station_panel_new(LnStationContext *ctx) {
    LnStationPanel *panel = g_malloc0(sizeof(LnStationPanel));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *title = gtk_label_new("LN Station");
    GtkWidget *identity_label = gtk_label_new("Identity / Serial");
    GtkWidget *operator_label = gtk_label_new("Operator");
    GtkWidget *position_label = gtk_label_new("Position");

    panel->ctx = ctx;
    panel->identity_entry = gtk_entry_new();
    panel->operator_entry = gtk_entry_new();
    panel->position_combo = gtk_combo_box_text_new();
    panel->status_label = gtk_label_new("Scan or enter an identity to attach results.");

    gtk_style_context_add_class(gtk_widget_get_style_context(title), "ln-station-title");

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), "Dial Up");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), "Dial Down");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), "Crown Up");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), "Crown Down");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), "Crown Left");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), "Crown Right");
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->position_combo), 0);

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    gtk_grid_attach(GTK_GRID(grid), title, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), identity_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->identity_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), operator_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->operator_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), position_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->position_combo, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), panel->status_label, 0, 4, 2, 1);

    g_signal_connect(panel->identity_entry, "changed", G_CALLBACK(on_identity_changed), panel);
    g_signal_connect(panel->operator_entry, "changed", G_CALLBACK(on_operator_changed), panel);
    g_signal_connect(panel->position_combo, "changed", G_CALLBACK(on_position_changed), panel);

    g_object_set_data_full(G_OBJECT(grid), "ln-station-panel", panel, g_free);
    return grid;
}

void ln_station_panel_set_status(GtkWidget *widget, const char *status) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (panel && panel->status_label) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), status ? status : "");
    }
}
