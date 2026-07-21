#include "ln_station_panel.h"
#include "ln_erp_config.h"

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
    GtkEntryCompletion *identity_completion;
    GtkListStore *identity_store;
    GtkWidget *movement_serial_entry;
    GtkWidget *movement_part_entry;
    GtkWidget *watch_unit_entry;
    GtkWidget *identity_status_entry;
    guint identity_search_timer;
    int suppress_identity_change;
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
    GtkWidget *erp_button;
    GtkWidget *bench_label;
    LnErpConfig erp_config;
    unsigned int completed_mask;
    GtkWidget *measurement_rate_label;
    GtkWidget *measurement_be_label;
    GtkWidget *measurement_amp_label;
    GtkWidget *measurement_bph_label;
    GtkWidget *capture_button;
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


static void set_status_and_flush(LnStationPanel *panel, const char *text) {
    if (!panel || !panel->status_label) return;
    gtk_label_set_text(GTK_LABEL(panel->status_label), text ? text : "");
    while (gtk_events_pending())
        gtk_main_iteration_do(FALSE);
}

static void update_erp_status(LnStationPanel *panel) {
    char bench_text[128];
    if (!panel) return;
    g_snprintf(bench_text, sizeof(bench_text), "●  %s%s",
               panel->erp_config.station_id[0] ? panel->erp_config.station_id : "Bench 01",
               panel->erp_config.enabled ? "  ·  ERP LINKED" : "  ·  LOCAL");
    gtk_label_set_text(GTK_LABEL(panel->bench_label), bench_text);
}

static void on_erp_settings(GtkButton *button, gpointer user_data) {
    (void)button;
    LnStationPanel *panel = (LnStationPanel *)user_data;
    GtkWidget *toplevel;
    char status[512] = {0};
    if (!panel) return;

    toplevel = gtk_widget_get_toplevel(panel->root);
    if (ln_erp_config_dialog_run(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                                 &panel->erp_config, status, sizeof(status))) {
        ln_erp_config_apply_to_station(&panel->erp_config, panel->ctx);
        update_erp_status(panel);
        gtk_label_set_text(GTK_LABEL(panel->status_label),
                           panel->erp_config.enabled ? "ERP connection saved and synchronization enabled." : "ERP connection saved; synchronization disabled.");
    } else if (status[0]) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), status);
    }
}


static char *json_unescape_copy(const char *start, size_t length) {
    GString *out = g_string_sized_new(length + 1);
    size_t i;
    for (i = 0; i < length; i++) {
        if (start[i] == '\\' && i + 1 < length) {
            i++;
            switch (start[i]) {
                case 'n': g_string_append_c(out, '\n'); break;
                case 'r': g_string_append_c(out, '\r'); break;
                case 't': g_string_append_c(out, '\t'); break;
                case '"': g_string_append_c(out, '"'); break;
                case '\\': g_string_append_c(out, '\\'); break;
                default: g_string_append_c(out, start[i]); break;
            }
        } else {
            g_string_append_c(out, start[i]);
        }
    }
    return g_string_free(out, FALSE);
}

static char *json_string_value(const char *json, const char *key) {
    char pattern[128];
    GRegex *regex;
    GMatchInfo *match = NULL;
    char *value = NULL;
    g_snprintf(pattern, sizeof(pattern), "\\\"%s\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"", key);
    regex = g_regex_new(pattern, G_REGEX_DOTALL, 0, NULL);
    if (regex && g_regex_match(regex, json ? json : "", 0, &match)) {
        gchar *raw = g_match_info_fetch(match, 1);
        value = json_unescape_copy(raw, strlen(raw));
        g_free(raw);
    }
    if (match) g_match_info_free(match);
    if (regex) g_regex_unref(regex);
    return value;
}

static void combo_set_active_text(GtkComboBoxText *combo, const char *text) {
    GtkTreeModel *model;
    GtkTreeIter iter;
    int index = 0;
    if (!combo || !text || !*text) return;
    model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    if (!model || !gtk_tree_model_get_iter_first(model, &iter)) return;
    do {
        gchar *value = NULL;
        gtk_tree_model_get(model, &iter, 0, &value, -1);
        if (value && g_strcmp0(value, text) == 0) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), index);
            g_free(value);
            return;
        }
        g_free(value);
        index++;
    } while (gtk_tree_model_iter_next(model, &iter));
}

static void set_readonly_entry(GtkWidget *entry, const char *value) {
    if (entry) gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
}

static void populate_identity_from_json(LnStationPanel *panel, const char *json) {
    char *found = NULL;
    char *identity_id = json_string_value(json, "identity_id");
    char *movement_serial = json_string_value(json, "movement_serial");
    char *manufacturer_serial = json_string_value(json, "manufacturer_serial");
    char *ln_part = json_string_value(json, "ln_part");
    char *item = json_string_value(json, "item");
    char *watch_unit = json_string_value(json, "current_watch_unit");
    char *work_order = json_string_value(json, "current_work_order");
    char *status = json_string_value(json, "lifecycle_status");
    char *grade = json_string_value(json, "functional_grade");
    char *qa = json_string_value(json, "qa_status");
    char *measurement = json_string_value(json, "recommended_measurement_type");
    char *profile = json_string_value(json, "recommended_profile");
    char status_text[256];

    (void)found;
    if (!identity_id) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Identity not found in ERP.");
        goto cleanup;
    }

    panel->suppress_identity_change = 1;
    gtk_entry_set_text(GTK_ENTRY(panel->identity_entry), identity_id);
    panel->suppress_identity_change = 0;
    ln_station_set_identity(panel->ctx, identity_id);

    set_readonly_entry(panel->movement_serial_entry,
                       movement_serial && *movement_serial ? movement_serial : manufacturer_serial);
    set_readonly_entry(panel->movement_part_entry,
                       ln_part && *ln_part ? ln_part : item);
    set_readonly_entry(panel->watch_unit_entry, watch_unit);
    set_readonly_entry(panel->work_order_entry, work_order);
    snprintf(panel->ctx->work_order, sizeof(panel->ctx->work_order), "%s", work_order ? work_order : "");

    g_snprintf(status_text, sizeof(status_text), "%s%s%s%s%s",
               status ? status : "",
               status && grade ? "  ·  " : "",
               grade ? grade : "",
               (status || grade) && qa ? "  ·  " : "",
               qa ? qa : "");
    set_readonly_entry(panel->identity_status_entry, status_text);

    combo_set_active_text(GTK_COMBO_BOX_TEXT(panel->measurement_type_combo), measurement);
    combo_set_active_text(GTK_COMBO_BOX_TEXT(panel->qa_standard_combo), profile);
    gtk_label_set_text(GTK_LABEL(panel->status_label), "ERP identity selected and movement data loaded.");

cleanup:
    g_free(identity_id); g_free(movement_serial); g_free(manufacturer_serial);
    g_free(ln_part); g_free(item); g_free(watch_unit); g_free(work_order);
    g_free(status); g_free(grade); g_free(qa); g_free(measurement); g_free(profile);
}

static void lookup_identity_now(LnStationPanel *panel, const char *identity) {
    char response[16384];
    int rc;
    if (!panel || !identity || !*identity || !panel->erp_config.enabled) return;
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Looking up identity in ERP...");
    rc = ln_erp_identity_lookup(&panel->erp_config, identity, response, sizeof(response));
    if (rc == 0)
        populate_identity_from_json(panel, response);
    else {
        char message[512];
        g_snprintf(message, sizeof(message), "ERP identity lookup failed (%d).", rc);
        gtk_label_set_text(GTK_LABEL(panel->status_label), message);
    }
}

static int update_identity_suggestions(LnStationPanel *panel, const char *json) {
    GRegex *regex;
    GMatchInfo *match = NULL;
    int count = 0;
    if (!panel || !panel->identity_store) return 0;
    gtk_list_store_clear(panel->identity_store);

    regex = g_regex_new("\\{[^{}]*\\\"identity_id\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"[^{}]*\\\"display\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"[^{}]*\\}", G_REGEX_DOTALL, 0, NULL);
    if (!regex) return 0;
    g_regex_match(regex, json ? json : "", 0, &match);
    while (match && g_match_info_matches(match)) {
        gchar *raw_id = g_match_info_fetch(match, 1);
        gchar *raw_display = g_match_info_fetch(match, 2);
        char *identity_id = json_unescape_copy(raw_id, strlen(raw_id));
        char *display = json_unescape_copy(raw_display, strlen(raw_display));
        GtkTreeIter iter;
        gtk_list_store_append(panel->identity_store, &iter);
        gtk_list_store_set(panel->identity_store, &iter, 0, display, 1, identity_id, -1);
        count++;
        g_free(raw_id); g_free(raw_display); g_free(identity_id); g_free(display);
        if (!g_match_info_next(match, NULL)) break;
    }
    if (match) g_match_info_free(match);
    g_regex_unref(regex);
    return count;
}

static gboolean identity_search_timeout(gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text;
    char response[32768];
    char message[1024];
    gint64 started_us;
    double elapsed_ms;
    int rc;
    int count = 0;

    panel->identity_search_timer = 0;
    if (!panel || !panel->erp_config.enabled || !panel->erp_config.auto_lookup)
        return G_SOURCE_REMOVE;

    text = gtk_entry_get_text(GTK_ENTRY(panel->identity_entry));
    if (!text || strlen(text) < 2)
        return G_SOURCE_REMOVE;

    g_snprintf(message, sizeof(message),
               "ERP search: '%s' · endpoint search_identities · timeout 10s...", text);
    set_status_and_flush(panel, message);
    fprintf(stderr, "[LNWS SEARCH] query='%s' station='%s'\n",
            text, panel->erp_config.station_id);
    fflush(stderr);

    started_us = g_get_monotonic_time();
    response[0] = '\0';
    rc = ln_erp_identity_search(&panel->erp_config, text, response, sizeof(response));
    elapsed_ms = (g_get_monotonic_time() - started_us) / 1000.0;

    if (rc == 0) {
        count = update_identity_suggestions(panel, response);
        if (count > 0) {
            g_snprintf(message, sizeof(message),
                       "ERP search complete: %d match%s in %.0f ms. Use arrow keys or click a result.",
                       count, count == 1 ? "" : "es", elapsed_ms);
        } else {
            g_snprintf(message, sizeof(message),
                       "ERP search complete: no matches for '%s' (%.0f ms). Response: %.420s",
                       text, elapsed_ms, response[0] ? response : "empty body");
        }
    } else if (rc == -408) {
        g_snprintf(message, sizeof(message),
                   "ERP search timed out after %.1f s. Check server reachability, TLS, or endpoint deployment. Detail: %.420s",
                   elapsed_ms / 1000.0, response);
    } else {
        g_snprintf(message, sizeof(message),
                   "ERP search failed: code %d after %.0f ms. Detail: %.500s",
                   rc, elapsed_ms, response[0] ? response : "No response body");
    }

    gtk_label_set_text(GTK_LABEL(panel->status_label), message);
    fprintf(stderr, "[LNWS SEARCH] %s\n", message);
    fflush(stderr);
    return G_SOURCE_REMOVE;
}

static gboolean on_identity_match_selected(GtkEntryCompletion *completion, GtkTreeModel *model,
                                           GtkTreeIter *iter, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gchar *identity_id = NULL;
    (void)completion;
    gtk_tree_model_get(model, iter, 1, &identity_id, -1);
    if (identity_id) {
        panel->suppress_identity_change = 1;
        gtk_entry_set_text(GTK_ENTRY(panel->identity_entry), identity_id);
        panel->suppress_identity_change = 0;
        lookup_identity_now(panel, identity_id);
        g_free(identity_id);
    }
    return TRUE;
}

static void on_identity_activate(GtkEntry *entry, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text = gtk_entry_get_text(entry);
    if (panel->identity_search_timer) {
        g_source_remove(panel->identity_search_timer);
        panel->identity_search_timer = 0;
    }
    lookup_identity_now(panel, text);
}

static void on_identity_changed(GtkEditable *editable, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(editable));
    ln_station_set_identity(panel->ctx, text);

    if (panel->suppress_identity_change) return;
    if (panel->identity_search_timer) {
        g_source_remove(panel->identity_search_timer);
        panel->identity_search_timer = 0;
    }

    if (text && *text) {
        if (!panel->erp_config.enabled) {
            gtk_label_set_text(GTK_LABEL(panel->status_label),
                               "ERP search unavailable: synchronization is disabled in ERP Connection settings.");
        } else if (!panel->erp_config.auto_lookup) {
            gtk_label_set_text(GTK_LABEL(panel->status_label),
                               "ERP auto-search is disabled. Press Enter to perform an exact lookup.");
        } else if (strlen(text) < 2) {
            gtk_label_set_text(GTK_LABEL(panel->status_label),
                               "Enter at least 2 characters to search ERP.");
        } else {
            gtk_label_set_text(GTK_LABEL(panel->status_label),
                               "ERP search queued (350 ms debounce)...");
            panel->identity_search_timer = g_timeout_add(350, identity_search_timeout, panel);
        }
    } else {
        if (panel->identity_store) gtk_list_store_clear(panel->identity_store);
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Ready. Scan or enter an identity.");
    }
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
    GtkWidget *bench = gtk_label_new("●  Bench 01  ·  LOCAL");
    panel->bench_label = bench;
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
    panel->movement_serial_entry = gtk_entry_new();
    panel->movement_part_entry = gtk_entry_new();
    panel->watch_unit_entry = gtk_entry_new();
    panel->identity_status_entry = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(panel->movement_serial_entry), FALSE);
    gtk_editable_set_editable(GTK_EDITABLE(panel->movement_part_entry), FALSE);
    gtk_editable_set_editable(GTK_EDITABLE(panel->watch_unit_entry), FALSE);
    gtk_editable_set_editable(GTK_EDITABLE(panel->identity_status_entry), FALSE);

    panel->identity_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    panel->identity_completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(panel->identity_completion, GTK_TREE_MODEL(panel->identity_store));
    gtk_entry_completion_set_text_column(panel->identity_completion, 0);
    gtk_entry_completion_set_minimum_key_length(panel->identity_completion, 2);
    gtk_entry_completion_set_popup_completion(panel->identity_completion, TRUE);
    gtk_entry_completion_set_inline_completion(panel->identity_completion, FALSE);
    gtk_entry_set_completion(GTK_ENTRY(panel->identity_entry), panel->identity_completion);
    panel->work_order_entry = gtk_entry_new();
    panel->operator_entry = gtk_entry_new();
    panel->position_combo = gtk_combo_box_text_new();
    panel->qa_standard_combo = gtk_combo_box_text_new();
    panel->temperature_combo = gtk_combo_box_text_new();
    panel->measurement_type_combo = gtk_combo_box_text_new();
    panel->erp_button = gtk_button_new_with_label("⚙  ERP Connection");

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

    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Search / Scan", panel->identity_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Movement S/N", panel->movement_serial_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Movement / Part", panel->movement_part_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Watch Unit", panel->watch_unit_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_card), make_row("ERP Status", panel->identity_status_entry), FALSE, FALSE, 0);
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
    panel->measurement_rate_label = gtk_label_new("Rate              waiting");
    panel->measurement_be_label = gtk_label_new("Beat Error        waiting");
    panel->measurement_amp_label = gtk_label_new("Amplitude         waiting");
    panel->measurement_bph_label = gtk_label_new("BPH               waiting");
    add_class(panel->measurement_rate_label, "lnws-green");
    gtk_widget_set_halign(panel->measurement_rate_label, GTK_ALIGN_START);
    gtk_widget_set_halign(panel->measurement_be_label, GTK_ALIGN_START);
    gtk_widget_set_halign(panel->measurement_amp_label, GTK_ALIGN_START);
    gtk_widget_set_halign(panel->measurement_bph_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(measurement_card), panel->measurement_rate_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(measurement_card), panel->measurement_be_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(measurement_card), panel->measurement_amp_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(measurement_card), panel->measurement_bph_label, FALSE, FALSE, 0);
    panel->capture_button = gtk_button_new_with_label("▶  Start Capture");
    add_class(panel->capture_button, "lnws-action");
    gtk_box_pack_start(GTK_BOX(measurement_card), panel->capture_button, FALSE, FALSE, 6);
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
    gtk_box_pack_start(GTK_BOX(data_card), panel->erp_button, FALSE, FALSE, 0);
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
    g_signal_connect(panel->identity_entry, "activate", G_CALLBACK(on_identity_activate), panel);
    g_signal_connect(panel->identity_completion, "match-selected", G_CALLBACK(on_identity_match_selected), panel);
    g_signal_connect(panel->work_order_entry, "changed", G_CALLBACK(on_work_order_changed), panel);
    g_signal_connect(panel->operator_entry, "changed", G_CALLBACK(on_operator_changed), panel);
    g_signal_connect(panel->position_combo, "changed", G_CALLBACK(on_position_changed), panel);
    g_signal_connect(panel->qa_standard_combo, "changed", G_CALLBACK(on_qa_standard_changed), panel);
    g_signal_connect(panel->temperature_combo, "changed", G_CALLBACK(on_temperature_changed), panel);
    g_signal_connect(panel->measurement_type_combo, "changed", G_CALLBACK(on_measurement_type_changed), panel);
    g_signal_connect(panel->next_button, "clicked", G_CALLBACK(on_next_position), root);
    g_signal_connect(panel->data_entry_toggle, "clicked", G_CALLBACK(on_toggle_data_entry), panel);
    g_signal_connect(panel->erp_button, "clicked", G_CALLBACK(on_erp_settings), panel);

    {
        char config_error[512] = {0};
        int config_result = ln_erp_config_load(&panel->erp_config, config_error, sizeof(config_error));
        ln_erp_config_apply_to_station(&panel->erp_config, panel->ctx);
        update_erp_status(panel);
        if (config_result < 0)
            gtk_label_set_text(GTK_LABEL(panel->status_label), config_error);
        else if (panel->erp_config.enabled)
            gtk_label_set_text(GTK_LABEL(panel->status_label), "ERP configuration loaded. Ready to scan an identity.");
    }

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

void ln_station_panel_refresh_measurement(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    LnCaptureStatus status;
    char rate_text[64], be_text[64], amp_text[64], bph_text[64];

    if (!panel) return;
    ln_station_capture_status(&status);

    if (!status.active) {
        gtk_label_set_text(GTK_LABEL(panel->measurement_rate_label), "Rate              waiting");
        gtk_label_set_text(GTK_LABEL(panel->measurement_be_label), "Beat Error        waiting");
        gtk_label_set_text(GTK_LABEL(panel->measurement_amp_label), "Amplitude         waiting");
        gtk_label_set_text(GTK_LABEL(panel->measurement_bph_label), "BPH               waiting");
        if (panel->capture_button)
            gtk_button_set_label(GTK_BUTTON(panel->capture_button), "▶  Start Capture");
        return;
    }

    if (status.complete) {
        snprintf(rate_text, sizeof(rate_text), "Rate              %+.1f s/d  ✓ (%d samples)", status.avg_rate_s_per_day, status.sample_count);
        snprintf(be_text, sizeof(be_text), "Beat Error        %.1f ms", status.avg_beat_error_ms);
        snprintf(amp_text, sizeof(amp_text), "Amplitude         %.0f deg", status.avg_amplitude_deg);
        snprintf(bph_text, sizeof(bph_text), "BPH               %.0f", status.avg_bph);
        if (panel->capture_button)
            gtk_button_set_label(GTK_BUTTON(panel->capture_button), "↻  Recapture");
    } else if (status.sample_count > 0) {
        snprintf(rate_text, sizeof(rate_text), "Rate              %+.1f s/d  (%.0fs/%ds)", status.avg_rate_s_per_day, status.elapsed_seconds, status.required_seconds);
        snprintf(be_text, sizeof(be_text), "Beat Error        %.1f ms", status.avg_beat_error_ms);
        snprintf(amp_text, sizeof(amp_text), "Amplitude         %.0f deg", status.avg_amplitude_deg);
        snprintf(bph_text, sizeof(bph_text), "BPH               %.0f", status.avg_bph);
        if (panel->capture_button)
            gtk_button_set_label(GTK_BUTTON(panel->capture_button), "↻  Recapture");
    } else {
        snprintf(rate_text, sizeof(rate_text), "Rate              capturing...");
        snprintf(be_text, sizeof(be_text), "Beat Error        capturing...");
        snprintf(amp_text, sizeof(amp_text), "Amplitude         capturing...");
        snprintf(bph_text, sizeof(bph_text), "BPH               capturing...");
        if (panel->capture_button)
            gtk_button_set_label(GTK_BUTTON(panel->capture_button), "↻  Recapture");
    }

    gtk_label_set_text(GTK_LABEL(panel->measurement_rate_label), rate_text);
    gtk_label_set_text(GTK_LABEL(panel->measurement_be_label), be_text);
    gtk_label_set_text(GTK_LABEL(panel->measurement_amp_label), amp_text);
    gtk_label_set_text(GTK_LABEL(panel->measurement_bph_label), bph_text);
}

static void on_capture_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *widget = GTK_WIDGET(user_data);
    ln_station_capture_begin();
    ln_station_panel_refresh_measurement(widget);
    ln_station_panel_set_status(widget, "Capture started -- keep the watch steady until the hold completes.");
}

void ln_station_panel_bind_capture(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (!panel || !panel->capture_button) return;
    g_signal_connect(panel->capture_button, "clicked", G_CALLBACK(on_capture_clicked), widget);
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
