#include "ln_station_panel.h"

#define LN_POSITION_COUNT 6

static const char *LN_POSITIONS[LN_POSITION_COUNT] = {
    "Dial Up",
    "Dial Down",
    "Crown Up",
    "Crown Down",
    "Crown Left",
    "Crown Right"
};

typedef struct {
    LnStationContext *ctx;
    GtkWidget *root;
    GtkWidget *identity_entry;
    GtkWidget *work_order_entry;
    GtkWidget *operator_entry;
    GtkWidget *position_combo;
    GtkWidget *qa_standard_combo;
    GtkWidget *temperature_combo;
    GtkWidget *measurement_type_combo;
    GtkWidget *status_label;
    GtkWidget *position_rows[LN_POSITION_COUNT];
    GtkWidget *position_status[LN_POSITION_COUNT];
    GtkWidget *data_entry_revealer;
    GtkWidget *data_entry_toggle;
    GtkWidget *save_button;
    GtkWidget *next_button;
    GtkWidget *complete_button;
    unsigned int completed_mask;
} LnStationPanel;

static void ln_station_panel_install_css(void) {
    static int installed = 0;
    if (installed) return;
    installed = 1;

    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css =
        ".lnws-sidebar { background: #111820; color: #f4f4f4; }"
        ".lnws-header { background: #07101a; border-bottom: 1px solid #25313d; padding: 10px; }"
        ".lnws-brand { color: #ffffff; font-size: 22px; font-weight: 700; letter-spacing: 1px; }"
        ".lnws-subtitle { color: #c9d0d6; font-size: 10px; letter-spacing: 0.5px; }"
        ".lnws-bench { color: #7ee36b; font-size: 12px; font-weight: 700; }"
        ".lnws-card { background: #17202a; border: 1px solid #2a3745; border-radius: 4px; padding: 10px; margin: 6px; }"
        ".lnws-section-title { color: #ffffff; font-size: 12px; font-weight: 700; letter-spacing: 0.5px; }"
        ".lnws-muted { color: #aeb8c2; font-size: 10px; }"
        ".lnws-ok { color: #42dc55; font-weight: 700; }"
        ".lnws-value { color: #ffffff; font-weight: 700; }"
        ".lnws-green { color: #42dc55; font-weight: 700; }"
        ".lnws-position-active { background: #173b87; border-radius: 3px; }"
        ".lnws-position-row { padding: 4px; }"
        ".lnws-action { font-size: 14px; font-weight: 700; padding: 12px; }"
        ".lnws-icon-action { font-size: 16px; font-weight: 700; padding: 4px 8px; }";

    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static void add_class(GtkWidget *widget, const char *class_name) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), class_name);
}

static GtkWidget *make_row(const char *label_text, GtkWidget *value_widget) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_size_request(label, 88, -1);
    gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), value_widget, TRUE, TRUE, 0);
    return row;
}

static GtkWidget *make_card(const char *title_text) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *title = gtk_label_new(title_text);
    add_class(card, "lnws-card");
    add_class(title, "lnws-section-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(card), title, FALSE, FALSE, 0);
    return card;
}

static void refresh_position_rows(LnStationPanel *panel) {
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(panel->position_combo));
    for (int i = 0; i < LN_POSITION_COUNT; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(panel->position_rows[i]);
        if (i == active)
            gtk_style_context_add_class(ctx, "lnws-position-active");
        else
            gtk_style_context_remove_class(ctx, "lnws-position-active");

        if (panel->completed_mask & (1u << i))
            gtk_label_set_text(GTK_LABEL(panel->position_status[i]), "✓");
        else if (i == active)
            gtk_label_set_text(GTK_LABEL(panel->position_status[i]), "▶");
        else
            gtk_label_set_text(GTK_LABEL(panel->position_status[i]), "○");
    }
}

static void on_identity_changed(GtkEditable *editable, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    ln_station_set_identity(panel->ctx, text);

    if (text && *text)
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Identity captured. Ready to measure.");
    else
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Ready. Scan or enter an identity.");
}

static void on_work_order_changed(GtkEditable *editable, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    if (!text) text = "";
    snprintf(panel->ctx->work_order, sizeof(panel->ctx->work_order), "%s", text);
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
    refresh_position_rows(panel);
}


static void on_temperature_changed(GtkComboBoxText *combo, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gchar *text = gtk_combo_box_text_get_active_text(combo);
    if (text) {
        ln_station_set_temperature_condition(text);
        g_free(text);
    }
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Temperature condition updated.");
}

static void on_measurement_type_changed(GtkComboBoxText *combo, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gchar *text = gtk_combo_box_text_get_active_text(combo);
    if (text) {
        ln_station_set_measurement_type(text);
        g_free(text);
    }
    gtk_label_set_text(GTK_LABEL(panel->status_label), ln_station_is_followup() ? "Follow-up mode enabled; baseline comparison active when ERP data is available." : "Initial measurement mode.");
}

static void on_qa_standard_changed(GtkComboBoxText *combo, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gchar *text = gtk_combo_box_text_get_active_text(combo);
    if (text) {
        ln_station_set_qa_standard(text);
        g_free(text);
    }
    gtk_label_set_text(GTK_LABEL(panel->status_label), "QA standard updated; 5-second pass timer reset.");
}

void ln_station_panel_advance_position(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (!panel) return;

    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(panel->position_combo));
    if (active < 0) active = 0;
    panel->completed_mask |= (1u << active);
    active = (active + 1) % LN_POSITION_COUNT;
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->position_combo), active);
    refresh_position_rows(panel);
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Moved to next timing position.");
}

static void on_next_position(GtkButton *button, gpointer user_data) {
    (void)button;
    ln_station_panel_advance_position(GTK_WIDGET(user_data));
}

static void on_toggle_data_entry(GtkButton *button, gpointer user_data) {
    (void)button;
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Data panel hidden. Use the side handle to show it again.");
}

GtkWidget *ln_station_panel_new(LnStationContext *ctx) {
    ln_station_panel_install_css();

    LnStationPanel *panel = g_malloc0(sizeof(LnStationPanel));
    panel->ctx = ctx;

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    panel->root = root;
    add_class(root, "lnws-sidebar");
    gtk_widget_set_size_request(root, 390, -1);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    add_class(header, "lnws-header");
    GtkWidget *brand = gtk_label_new("LN WATCHMAKER STATION");
    GtkWidget *subtitle = gtk_label_new("SCAN FIRST. MEASURE SECOND. EVERY RESULT BELONGS TO AN IDENTITY.");
    GtkWidget *bench = gtk_label_new("●  Bench 01");
    add_class(brand, "lnws-brand");
    add_class(subtitle, "lnws-subtitle");
    add_class(bench, "lnws-bench");
    gtk_widget_set_halign(brand, GTK_ALIGN_START);
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_widget_set_halign(bench, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(header), brand, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), bench, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    GtkWidget *identity_card = make_card("⌃  IDENTITY     ✓ IDENTIFIED");
    panel->identity_entry = gtk_entry_new();
    panel->work_order_entry = gtk_entry_new();
    panel->operator_entry = gtk_entry_new();
    panel->position_combo = gtk_combo_box_text_new();
    panel->qa_standard_combo = gtk_combo_box_text_new();
    panel->temperature_combo = gtk_combo_box_text_new();
    panel->measurement_type_combo = gtk_combo_box_text_new();

    gtk_entry_set_placeholder_text(GTK_ENTRY(panel->identity_entry), "LN-SER-00025 / watch serial");
    gtk_entry_set_placeholder_text(GTK_ENTRY(panel->work_order_entry), "LN-MFG-00042 / optional");
    gtk_entry_set_placeholder_text(GTK_ENTRY(panel->operator_entry), "operator badge / initials");

    for (int i = 0; i < LN_POSITION_COUNT; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->position_combo), LN_POSITIONS[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->position_combo), 0);

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->qa_standard_combo), "Workshop");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->qa_standard_combo), "Precision");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->qa_standard_combo), "Signature");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->qa_standard_combo), "Observatory");
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->qa_standard_combo), 0);

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->temperature_combo), "Room / 23C");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->temperature_combo), "Cold / 8C");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->temperature_combo), "Warm / 38C");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->temperature_combo), "Custom");
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->temperature_combo), 0);

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->measurement_type_combo), "Initial Certification");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->measurement_type_combo), "Follow-up Certification");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->measurement_type_combo), "Service Check");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->measurement_type_combo), "Regulation Check");
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->measurement_type_combo), 0);

    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Watch / Serial", panel->identity_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Work Order", panel->work_order_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Operator", panel->operator_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Position", panel->position_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("QA Standard", panel->qa_standard_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Temperature", panel->temperature_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Measurement", panel->measurement_type_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), identity_card, FALSE, FALSE, 0);

    GtkWidget *session_card = make_card("▣  SESSION");
    GtkWidget *session_id = gtk_label_new("Session ID     LN-TIME-auto");
    GtkWidget *started = gtk_label_new("Started        Current run");
    GtkWidget *positions = gtk_label_new("Positions      0 / 6 completed");
    add_class(session_id, "lnws-muted");
    add_class(started, "lnws-muted");
    add_class(positions, "lnws-value");
    gtk_widget_set_halign(session_id, GTK_ALIGN_START);
    gtk_widget_set_halign(started, GTK_ALIGN_START);
    gtk_widget_set_halign(positions, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(session_card), session_id, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(session_card), started, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(session_card), positions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), session_card, FALSE, FALSE, 0);

    GtkWidget *positions_card = make_card("⚑  POSITIONS (6)");
    for (int i = 0; i < LN_POSITION_COUNT; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *num = gtk_label_new(NULL);
        GtkWidget *name = gtk_label_new(LN_POSITIONS[i]);
        GtkWidget *status = gtk_label_new("○");
        char nbuf[8];
        snprintf(nbuf, sizeof(nbuf), "%d", i + 1);
        gtk_label_set_text(GTK_LABEL(num), nbuf);
        add_class(row, "lnws-position-row");
        gtk_widget_set_halign(name, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(row), num, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), name, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(row), status, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(positions_card), row, FALSE, FALSE, 0);
        panel->position_rows[i] = row;
        panel->position_status[i] = status;
    }
    gtk_box_pack_start(GTK_BOX(root), positions_card, FALSE, FALSE, 0);

    GtkWidget *measurement_card = make_card("⌁  MEASUREMENT");
    GtkWidget *rate = gtk_label_new("Rate              waiting");
    GtkWidget *be = gtk_label_new("Beat Error        waiting");
    GtkWidget *amp = gtk_label_new("Amplitude         waiting");
    GtkWidget *bph = gtk_label_new("BPH               waiting");
    add_class(rate, "lnws-green");
    gtk_widget_set_halign(rate, GTK_ALIGN_START);
    gtk_widget_set_halign(be, GTK_ALIGN_START);
    gtk_widget_set_halign(amp, GTK_ALIGN_START);
    gtk_widget_set_halign(bph, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(measurement_card), rate, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(measurement_card), be, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(measurement_card), amp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(measurement_card), bph, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), measurement_card, FALSE, FALSE, 0);

    GtkWidget *data_card = make_card("⌨  DATA ENTRY");
    panel->data_entry_toggle = gtk_button_new_with_label("Hide Data Panel");
    panel->data_entry_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(panel->data_entry_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(panel->data_entry_revealer), TRUE);
    GtkWidget *data_help = gtk_label_new("Scan the identity field with a USB barcode scanner. Operator and work order can remain blank for simple timegrapher use.");
    gtk_label_set_line_wrap(GTK_LABEL(data_help), TRUE);
    add_class(data_help, "lnws-muted");
    gtk_container_add(GTK_CONTAINER(panel->data_entry_revealer), data_help);
    gtk_box_pack_start(GTK_BOX(data_card), panel->data_entry_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(data_card), panel->data_entry_revealer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), data_card, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(root), spacer, TRUE, TRUE, 0);

    panel->save_button = gtk_button_new_with_label("▣  Save Reading");
    panel->next_button = gtk_button_new_with_label("→  Next Position");
    panel->complete_button = gtk_button_new_with_label("⚑  Complete Session");
    add_class(panel->save_button, "lnws-action");
    add_class(panel->next_button, "lnws-action");
    add_class(panel->complete_button, "lnws-action");
    gtk_box_pack_start(GTK_BOX(root), panel->save_button, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(root), panel->next_button, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(root), panel->complete_button, FALSE, FALSE, 6);

    panel->status_label = gtk_label_new("Ready. Scan or enter an identity.");
    add_class(panel->status_label, "lnws-muted");
    gtk_widget_set_halign(panel->status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), panel->status_label, FALSE, FALSE, 8);

    g_signal_connect(panel->identity_entry, "changed", G_CALLBACK(on_identity_changed), panel);
    g_signal_connect(panel->work_order_entry, "changed", G_CALLBACK(on_work_order_changed), panel);
    g_signal_connect(panel->operator_entry, "changed", G_CALLBACK(on_operator_changed), panel);
    g_signal_connect(panel->position_combo, "changed", G_CALLBACK(on_position_changed), panel);
    g_signal_connect(panel->qa_standard_combo, "changed", G_CALLBACK(on_qa_standard_changed), panel);
    g_signal_connect(panel->temperature_combo, "changed", G_CALLBACK(on_temperature_changed), panel);
    g_signal_connect(panel->measurement_type_combo, "changed", G_CALLBACK(on_measurement_type_changed), panel);
    g_signal_connect(panel->next_button, "clicked", G_CALLBACK(on_next_position), root);
    g_signal_connect(panel->data_entry_toggle, "clicked", G_CALLBACK(on_toggle_data_entry), panel);

    g_object_set_data_full(G_OBJECT(root), "ln-station-panel", panel, g_free);
    refresh_position_rows(panel);
    return root;
}

void ln_station_panel_set_status(GtkWidget *widget, const char *status) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (panel && panel->status_label) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), status ? status : "");
    }
}

void ln_station_panel_bind_actions(GtkWidget *widget, GCallback save_callback, GCallback next_callback, GCallback complete_callback, gpointer user_data) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (!panel) return;
    if (save_callback)
        g_signal_connect(panel->save_button, "clicked", save_callback, user_data);
    if (next_callback)
        g_signal_connect(panel->next_button, "clicked", next_callback, user_data);
    if (complete_callback)
        g_signal_connect(panel->complete_button, "clicked", complete_callback, user_data);
}

void ln_station_panel_bind_sidebar_toggle(GtkWidget *widget, GCallback toggle_callback, gpointer user_data) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (!panel || !toggle_callback) return;
    g_signal_connect(panel->data_entry_toggle, "clicked", toggle_callback, user_data);
}
