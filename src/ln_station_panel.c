#include "ln_station_panel.h"
#include "ln_erp_config.h"
#include "ln_operator.h"
#include "ln_scanner.h"
#include <windows.h>
#include <shellapi.h>

/* Position names/count now live in ln_station.h (ln_station_position_names)
 * as the single source of truth shared with the full-session export/submit
 * path -- kept as a local alias here so the rest of this file's many
 * LN_POSITION_COUNT references don't need to change. */
#define LN_POSITION_COUNT LN_STATION_POSITION_COUNT
#define LN_POSITIONS ln_station_position_names

typedef struct {
    LnStationContext *ctx;
    GtkWidget *root;
    GtkWidget *identity_entry;
    GtkWidget *open_in_erp_button;
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
    GtkWidget *operator_button; /* "Sign in" / "Sign out", next to the operator entry */
    GtkWidget *position_combo;
    GtkWidget *qa_standard_combo;
    GtkWidget *temperature_combo;
    GtkWidget *measurement_type_combo;
    GtkWidget *status_label;
    /* Positions table (see refresh_position_rows): position_rows[i] is the
     * position-name cell specifically, kept under this name so the existing
     * active-row-highlight logic didn't need renaming -- position_num[i]
     * and position_status[i] are the other two cells in that same row. All
     * three get the active-row CSS class together (see
     * set_position_row_active) since a GtkGrid has no single "row" widget
     * of its own to attach a style class to. */
    GtkWidget *position_num[LN_POSITION_COUNT];
    GtkWidget *position_rows[LN_POSITION_COUNT];
    GtkWidget *position_status[LN_POSITION_COUNT];
    GtkWidget *identity_details_revealer;
    GtkWidget *identity_details_toggle;
    GtkWidget *data_entry_revealer;
    GtkWidget *data_entry_toggle;
    GtkWidget *save_button;
    GtkWidget *prev_button;
    GtkWidget *next_button;
    GtkWidget *complete_button;
    GtkWidget *erp_button;
    GtkWidget *scanner_button;
    GtkWidget *bench_label;
    LnErpConfig erp_config;
    LnScannerConfig scanner_config;
    GtkWidget *measurement_rate_label;
    GtkWidget *measurement_be_label;
    GtkWidget *measurement_amp_label;
    GtkWidget *measurement_bph_label;
    GtkWidget *capture_button;
    GtkWidget *positions_completed_label;
    GtkWidget *stats_label;
    /* Whatever identity the reset-on-change logic last saw, so retrying a
     * lookup (e.g. after a network blip) on the SAME identity doesn't wipe
     * in-progress capture data -- only a genuinely different value should
     * trigger a reset. See lookup_identity_now(). */
    char last_confirmed_identity[128];
} LnStationPanel;

/* Defined further down (needs refresh_position_rows/refresh_measurement);
 * forward-declared so lookup_identity_now() can call it. */
static void reset_session_state(LnStationPanel *panel, int clear_identity_entry);

/* Defined further down; forward-declared so the scanner callbacks (which
 * need to appear near on_erp_settings, well before lookup_identity_now's
 * own definition) can call it. */
static void lookup_identity_now(LnStationPanel *panel, const char *identity);

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
        ".lnws-card { background: #17202a; border: 1px solid #2a3745; border-radius: 4px; padding: 6px; margin: 3px; }"
        ".lnws-section-title { color: #ffffff; font-size: 12px; font-weight: 700; letter-spacing: 0.5px; }"
        ".lnws-muted { color: #aeb8c2; font-size: 10px; }"
        ".lnws-ok { color: #42dc55; font-weight: 700; }"
        ".lnws-value { color: #ffffff; font-weight: 700; }"
        ".lnws-green { color: #42dc55; font-weight: 700; }"
        ".lnws-position-active { background: #173b87; border-radius: 3px; }"
        ".lnws-position-cell { padding: 2px 4px; }"
        ".lnws-action { font-size: 14px; font-weight: 700; padding: 12px; }"
        ".lnws-icon-action { font-size: 16px; font-weight: 700; padding: 4px 8px; }"
        ".lnws-toggle { font-size: 10px; padding: 2px 6px; }";

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

/* Refreshes the POSITIONS list (icon + captured data per row, matching
 * the actual stored LnPositionResult rather than just "was Next Position
 * clicked past it"), the "Positions X / 6 completed" counter, and the
 * ACROSS POSITIONS card's delta/average/min/max/median summary. All
 * three are driven by the same per-position result data, so they refresh
 * together whenever a capture completes or the position changes. */
static void refresh_position_rows(LnStationPanel *panel) {
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(panel->position_combo));
    int completed_count = 0;

    for (int i = 0; i < LN_POSITION_COUNT; i++) {
        LnPositionResult result;
        int has_result = ln_station_get_position_result(LN_POSITIONS[i], &result);
        char status_text[64];
        GtkWidget *row_widgets[3] = {panel->position_num[i], panel->position_rows[i], panel->position_status[i]};

        for (int w = 0; w < 3; w++) {
            GtkStyleContext *ctx = gtk_widget_get_style_context(row_widgets[w]);
            if (i == active)
                gtk_style_context_add_class(ctx, "lnws-position-active");
            else
                gtk_style_context_remove_class(ctx, "lnws-position-active");
        }

        if (has_result) {
            completed_count++;
            snprintf(status_text, sizeof(status_text), "✓  %+.1f s/d", result.rate_s_per_day);
        } else if (i == active) {
            snprintf(status_text, sizeof(status_text), "▶");
        } else {
            snprintf(status_text, sizeof(status_text), "○");
        }
        gtk_label_set_text(GTK_LABEL(panel->position_status[i]), status_text);
    }

    if (panel->positions_completed_label) {
        char positions_text[64];
        snprintf(positions_text, sizeof(positions_text), "Positions      %d / %d completed", completed_count, LN_POSITION_COUNT);
        gtk_label_set_text(GTK_LABEL(panel->positions_completed_label), positions_text);
    }

    if (panel->stats_label) {
        LnPositionRateStats stats;
        ln_station_position_rate_stats(&stats);
        if (stats.count > 0) {
            char stats_text[256];
            snprintf(stats_text, sizeof(stats_text),
                     "Rate across %d position%s (s/d)\nDelta   %+.1f\nAverage %+.1f\nMin     %+.1f\nMax     %+.1f\nMedian  %+.1f",
                     stats.count, stats.count == 1 ? "" : "s",
                     stats.delta_rate_s_per_day, stats.avg_rate_s_per_day,
                     stats.min_rate_s_per_day, stats.max_rate_s_per_day, stats.median_rate_s_per_day);
            gtk_label_set_text(GTK_LABEL(panel->stats_label), stats_text);
        } else {
            gtk_label_set_text(GTK_LABEL(panel->stats_label), "No completed positions yet.");
        }
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

/* Delivered on the GTK main thread (see ln_scanner.h) whenever the COM
 * scanner reads a complete value. Mirrors on_identity_activate exactly --
 * sets the field so the operator sees what was scanned, then triggers the
 * same lookup path a manual Enter-key confirm would, but without depending
 * on the identity field actually having keyboard focus at scan time. */
static void on_scanner_scan_received(const char *value, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    if (!panel || !value || !*value) return;
    panel->suppress_identity_change = 1;
    gtk_entry_set_text(GTK_ENTRY(panel->identity_entry), value);
    panel->suppress_identity_change = 0;
    lookup_identity_now(panel, value);
}

static void on_scanner_status(const char *message, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    if (!panel || !message) return;
    gtk_label_set_text(GTK_LABEL(panel->status_label), message);
}

/* Opens the currently-loaded identity's own record in the ERP, in the
 * system's default browser -- e.g. https://erp.example.com/desk/
 * ln-inventory-identity/LN-ST2130-CV-00002. LN Inventory Identity autonames
 * off identity_id directly (field:identity_id), so the docname to put in
 * the URL is exactly whatever's in the Search/Scan field -- no separate
 * lookup needed. No escaping applied to the identity_id: it's always
 * either scanned/generated by this same system or typed in by an operator
 * as a plain alphanumeric-and-hyphens code, never containing characters
 * that would need URL-encoding. */
static void on_open_in_erp(GtkButton *button, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    const char *identity_id;
    char url[900];
    (void)button;
    if (!panel) return;

    identity_id = gtk_entry_get_text(GTK_ENTRY(panel->identity_entry));
    if (!identity_id || !*identity_id) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Scan or select an identity first.");
        return;
    }
    if (!panel->erp_config.base_url[0]) {
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Set an ERP Base URL first (ERP Connection).");
        return;
    }

    g_snprintf(url, sizeof(url), "%s/desk/ln-inventory-identity/%s", panel->erp_config.base_url, identity_id);
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

static void on_scanner_settings(GtkButton *button, gpointer user_data) {
    (void)button;
    LnStationPanel *panel = (LnStationPanel *)user_data;
    GtkWidget *toplevel;
    char status[512] = {0};
    if (!panel) return;

    toplevel = gtk_widget_get_toplevel(panel->root);
    if (ln_scanner_dialog_run(GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL,
                              &panel->scanner_config, status, sizeof(status))) {
        ln_scanner_start(&panel->scanner_config, on_scanner_scan_received, on_scanner_status, panel);
        if (!panel->scanner_config.enabled)
            gtk_label_set_text(GTK_LABEL(panel->status_label), "Scanner settings saved; COM scanning disabled.");
        /* If enabled, on_scanner_status will report "Connected to COMx..." or
         * a failure shortly, from the background thread -- no need to set a
         * status message here too. */
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
    if (!panel || !identity || !*identity) return;

    /* A genuinely different identity than the last one confirmed here means
     * a new physical unit is on the bench -- reset before doing anything
     * else (including the ERP lookup below, which may be disabled or fail;
     * the reset itself doesn't depend on ERP at all). Comparing against
     * last_confirmed_identity rather than ctx->identity_id specifically:
     * ctx->identity_id is updated on every keystroke by on_identity_changed,
     * so by the time this runs it already equals `identity` and would make
     * the comparison useless. Retrying a lookup on the same identity (e.g.
     * after a network blip) must not wipe in-progress capture data, which
     * is exactly what comparing against the last-confirmed value prevents. */
    if (g_strcmp0(panel->last_confirmed_identity, identity) != 0) {
        reset_session_state(panel, 0);
        g_strlcpy(panel->last_confirmed_identity, identity, sizeof(panel->last_confirmed_identity));
    }

    if (!panel->erp_config.enabled) return;
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

/* Brings the Operator row in line with the sign-in state. While someone is
 * signed in the entry is read-only: a result names an operator and the ERP
 * only attests it if that is the one who signed in, so the field must not
 * be changeable underneath the token. */
static void refresh_operator_widgets(LnStationPanel *panel) {
    int signed_in = ln_station_operator_signed_in(panel->ctx);
    gtk_button_set_label(GTK_BUTTON(panel->operator_button), signed_in ? "Sign out" : "Sign in");
    gtk_editable_set_editable(GTK_EDITABLE(panel->operator_entry), !signed_in);
    if (signed_in) {
        gtk_entry_set_text(GTK_ENTRY(panel->operator_entry), panel->ctx->operator_id);
        gtk_widget_set_tooltip_text(panel->operator_entry,
                                    panel->ctx->operator_name[0] ? panel->ctx->operator_name : "Signed in");
        gtk_widget_set_tooltip_text(panel->operator_button, "Sign out");
    } else {
        gtk_widget_set_tooltip_text(panel->operator_entry, NULL);
        gtk_widget_set_tooltip_text(panel->operator_button,
                                    "Sign in with your Operator ID and PIN so the ERP can attest your results");
    }
}

/* Asks for the Operator ID and PIN and signs in. The dialog stays open
 * after a refusal (wrong PIN, locked ...) with the ERP's own words shown,
 * so the operator can try again or cancel. Returns 1 if signed in. The PIN
 * is wiped from our copy as soon as the call returns. */
static int run_operator_sign_in_dialog(LnStationPanel *panel) {
    GtkWidget *parent = gtk_widget_get_toplevel(panel->root);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Operator sign-in", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "Cancel", GTK_RESPONSE_CANCEL, "Sign in", GTK_RESPONSE_OK, NULL);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *id_label = gtk_label_new("Operator ID");
    GtkWidget *pin_label = gtk_label_new("PIN");
    GtkWidget *id_entry = gtk_entry_new();
    GtkWidget *pin_entry = gtk_entry_new();
    GtkWidget *message_label = gtk_label_new("");
    int signed_in = 0;

    gtk_widget_set_halign(id_label, GTK_ALIGN_START);
    gtk_widget_set_halign(pin_label, GTK_ALIGN_START);
    gtk_entry_set_text(GTK_ENTRY(id_entry), panel->ctx->operator_id);
    gtk_entry_set_visibility(GTK_ENTRY(pin_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(pin_entry), GTK_INPUT_PURPOSE_PIN);
    gtk_entry_set_max_length(GTK_ENTRY(pin_entry), 8);
    gtk_entry_set_placeholder_text(GTK_ENTRY(pin_entry), "6 to 8 digits");
    gtk_entry_set_activates_default(GTK_ENTRY(id_entry), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(pin_entry), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(message_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(message_label), 40);
    gtk_widget_set_halign(message_label, GTK_ALIGN_START);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_grid_attach(GTK_GRID(grid), id_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), id_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pin_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pin_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), message_label, 0, 2, 2, 1);
    gtk_container_add(GTK_CONTAINER(area), grid);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(panel->ctx->operator_id[0] ? pin_entry : id_entry);

    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        char message[256];
        char *pin = g_strdup(gtk_entry_get_text(GTK_ENTRY(pin_entry)));
        int rc;

        gtk_entry_set_text(GTK_ENTRY(pin_entry), "");
        gtk_label_set_text(GTK_LABEL(message_label), "Signing in...");
        gtk_widget_set_sensitive(dialog, FALSE);
        while (gtk_events_pending()) gtk_main_iteration(); /* show the wait; the call below blocks */

        rc = ln_station_operator_sign_in(panel->ctx, gtk_entry_get_text(GTK_ENTRY(id_entry)), pin, message, sizeof(message));
        ln_operator_wipe(pin, strlen(pin));
        g_free(pin);

        gtk_widget_set_sensitive(dialog, TRUE);
        if (rc == 0) {
            signed_in = 1;
            break;
        }
        gtk_label_set_text(GTK_LABEL(message_label), message);
        gtk_widget_grab_focus(pin_entry);
    }
    gtk_widget_destroy(dialog);
    return signed_in;
}

static void on_operator_button_clicked(GtkButton *button, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    (void)button;
    if (ln_station_operator_signed_in(panel->ctx)) {
        ln_station_operator_sign_out(panel->ctx);
        refresh_operator_widgets(panel);
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Operator signed out.");
        return;
    }
    if (run_operator_sign_in_dialog(panel)) {
        char status[256];
        refresh_operator_widgets(panel);
        g_snprintf(status, sizeof(status), "Signed in as %s.",
                   panel->ctx->operator_name[0] ? panel->ctx->operator_name : panel->ctx->operator_id);
        gtk_label_set_text(GTK_LABEL(panel->status_label), status);
    }
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

    if (active == LN_POSITION_COUNT - 1) {
        /* Last position -- nothing left to advance to. Wrapping back to
         * position 1 here would silently start a new capture over
         * whatever that position already recorded. Stop sampling instead
         * and leave the reading as-is, ready for Save Reading / Complete
         * Session. */
        ln_station_capture_stop();
        refresh_position_rows(panel);
        ln_station_panel_refresh_measurement(widget);
        gtk_label_set_text(GTK_LABEL(panel->status_label), "All positions captured. Save this reading, then Complete Session.");
        return;
    }

    active = (active + 1) % LN_POSITION_COUNT;
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->position_combo), active);
    refresh_position_rows(panel);
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Moved to next timing position.");
}

static void on_next_position(GtkButton *button, gpointer user_data) {
    (void)button;
    ln_station_panel_advance_position(GTK_WIDGET(user_data));
}

/* Mirrors ln_station_panel_advance_position, one position earlier -- lets
 * an operator recover from an accidental double-click on Next Position
 * (or just re-check/redo the previous reading) without having to cycle
 * all the way around via the position combo. */
void ln_station_panel_retreat_position(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (!panel) return;

    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(panel->position_combo));
    if (active < 0) active = 0;

    if (active == 0) {
        /* First position -- nothing before it to go back to. Wrapping to
         * the last position here would silently start overwriting whatever
         * that position already recorded -- the same risk guarded against
         * at the other end in ln_station_panel_advance_position. */
        ln_station_capture_stop();
        refresh_position_rows(panel);
        ln_station_panel_refresh_measurement(widget);
        gtk_label_set_text(GTK_LABEL(panel->status_label), "Already at the first position.");
        return;
    }

    active = active - 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->position_combo), active);
    refresh_position_rows(panel);
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Moved to previous timing position.");
}

static void on_prev_position(GtkButton *button, gpointer user_data) {
    (void)button;
    ln_station_panel_retreat_position(GTK_WIDGET(user_data));
}

static void on_toggle_data_entry(GtkButton *button, gpointer user_data) {
    (void)button;
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gtk_label_set_text(GTK_LABEL(panel->status_label), "Data panel hidden. Use the side handle to show it again.");
}

/* Collapses everything in the IDENTITY card except the Search/Scan field
 * itself -- once an identity's confirmed, an operator rarely needs to keep
 * staring at its Movement S/N/Watch Unit/etc. every position, and freeing
 * that vertical space is most of what makes the sidebar fit a 1080p
 * screen. Search/Scan stays outside the revealer regardless, since that's
 * the one field that must always be reachable (barcode scans land there). */
static void on_toggle_identity_details(GtkButton *button, gpointer user_data) {
    LnStationPanel *panel = (LnStationPanel *)user_data;
    gboolean now_visible;
    (void)button;
    if (!panel || !panel->identity_details_revealer) return;

    now_visible = !gtk_revealer_get_reveal_child(GTK_REVEALER(panel->identity_details_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(panel->identity_details_revealer), now_visible);
    gtk_button_set_label(GTK_BUTTON(panel->identity_details_toggle),
                          now_visible ? "▾  Hide Details" : "▸  Show Details");
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
    panel->operator_button = gtk_button_new_with_label("Sign in");
    gtk_widget_set_tooltip_text(panel->operator_button, "Sign in with your Operator ID and PIN so the ERP can attest your results");
    panel->position_combo = gtk_combo_box_text_new();
    panel->qa_standard_combo = gtk_combo_box_text_new();
    panel->temperature_combo = gtk_combo_box_text_new();
    panel->measurement_type_combo = gtk_combo_box_text_new();
    panel->erp_button = gtk_button_new_with_label("⚙  ERP Connection");
    panel->scanner_button = gtk_button_new_with_label("⚙  Scanner Connection");

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
    /* Bare-movement pre-casing screening pass -- sorts a movement's
     * functional_grade (suggested, not auto-applied) rather than
     * producing a finished-watch certificate. Appended rather than
     * inserted first so the existing default selection doesn't change
     * for operators used to today's list. */
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->measurement_type_combo), "Qualification");
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->measurement_type_combo), 0);

    gtk_box_pack_start(GTK_BOX(identity_card), make_row("Search / Scan", panel->identity_entry), FALSE, FALSE, 0);

    /* Kept outside the details revealer, on the same row as the toggle --
     * this is a quick per-identity action an operator would want reachable
     * regardless of whether the details section is collapsed. */
    GtkWidget *identity_actions_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    panel->identity_details_toggle = gtk_button_new_with_label("▾  Hide Details");
    add_class(panel->identity_details_toggle, "lnws-toggle");
    panel->open_in_erp_button = gtk_button_new_with_label("🔗  Open in ERP");
    add_class(panel->open_in_erp_button, "lnws-toggle");
    gtk_box_pack_start(GTK_BOX(identity_actions_row), panel->identity_details_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_actions_row), panel->open_in_erp_button, FALSE, FALSE, 0);
    gtk_widget_set_halign(identity_actions_row, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(identity_card), identity_actions_row, FALSE, FALSE, 0);

    panel->identity_details_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(panel->identity_details_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(panel->identity_details_revealer), TRUE);
    GtkWidget *identity_details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Movement S/N", panel->movement_serial_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Movement / Part", panel->movement_part_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Watch Unit", panel->watch_unit_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("ERP Status", panel->identity_status_entry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Work Order", panel->work_order_entry), FALSE, FALSE, 0);
    {
        GtkWidget *operator_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_pack_start(GTK_BOX(operator_box), panel->operator_entry, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(operator_box), panel->operator_button, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Operator", operator_box), FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Position", panel->position_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("QA Standard", panel->qa_standard_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Temperature", panel->temperature_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(identity_details_box), make_row("Measurement", panel->measurement_type_combo), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(panel->identity_details_revealer), identity_details_box);
    gtk_box_pack_start(GTK_BOX(identity_card), panel->identity_details_revealer, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), identity_card, FALSE, FALSE, 0);

    GtkWidget *session_card = make_card("▣  SESSION");
    GtkWidget *session_id = gtk_label_new("Session ID     LN-TIME-auto");
    GtkWidget *started = gtk_label_new("Started        Current run");
    panel->positions_completed_label = gtk_label_new("Positions      0 / 6 completed");
    add_class(session_id, "lnws-muted");
    add_class(started, "lnws-muted");
    add_class(panel->positions_completed_label, "lnws-value");
    gtk_widget_set_halign(session_id, GTK_ALIGN_START);
    gtk_widget_set_halign(started, GTK_ALIGN_START);
    gtk_widget_set_halign(panel->positions_completed_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(session_card), session_id, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(session_card), started, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(session_card), panel->positions_completed_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), session_card, FALSE, FALSE, 0);

    /* A GtkGrid instead of one padded GtkBox per position -- GtkGrid aligns
     * columns natively (widest cell in each column sets that column's
     * width across every row) and has no per-row widget/padding overhead,
     * so 6 positions take noticeably less vertical space than 6 separate
     * boxed rows did, which is most of the point on a 1080p screen. */
    GtkWidget *positions_card = make_card("⚑  POSITIONS (6)");
    GtkWidget *positions_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(positions_grid), 1);
    gtk_grid_set_column_spacing(GTK_GRID(positions_grid), 8);

    GtkWidget *header_num = gtk_label_new("#");
    GtkWidget *header_name = gtk_label_new("Position");
    GtkWidget *header_status = gtk_label_new("Status");
    add_class(header_num, "lnws-muted");
    add_class(header_name, "lnws-muted");
    add_class(header_status, "lnws-muted");
    gtk_widget_set_halign(header_name, GTK_ALIGN_START);
    gtk_widget_set_halign(header_status, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(positions_grid), header_num, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(positions_grid), header_name, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(positions_grid), header_status, 2, 0, 1, 1);

    for (int i = 0; i < LN_POSITION_COUNT; i++) {
        GtkWidget *num = gtk_label_new(NULL);
        GtkWidget *name = gtk_label_new(LN_POSITIONS[i]);
        GtkWidget *status = gtk_label_new("○");
        char nbuf[8];
        snprintf(nbuf, sizeof(nbuf), "%d", i + 1);
        gtk_label_set_text(GTK_LABEL(num), nbuf);
        add_class(num, "lnws-position-cell");
        add_class(name, "lnws-position-cell");
        add_class(status, "lnws-position-cell");
        gtk_widget_set_halign(num, GTK_ALIGN_FILL);
        gtk_label_set_xalign(GTK_LABEL(num), 0.5);
        /* FILL (not START/END) so each label's own box actually spans its
         * whole grid cell -- the active-row CSS background paints behind
         * the label's allocated box, not just its glyphs, so START/END
         * alignment left three disconnected highlighted patches sized to
         * the text instead of one continuous row band. xalign keeps the
         * text itself positioned the same as before within that wider box. */
        gtk_widget_set_halign(name, GTK_ALIGN_FILL);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0);
        gtk_widget_set_halign(status, GTK_ALIGN_FILL);
        gtk_label_set_xalign(GTK_LABEL(status), 1.0);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_grid_attach(GTK_GRID(positions_grid), num, 0, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(positions_grid), name, 1, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(positions_grid), status, 2, i + 1, 1, 1);
        panel->position_num[i] = num;
        panel->position_rows[i] = name;
        panel->position_status[i] = status;
    }
    gtk_box_pack_start(GTK_BOX(positions_card), positions_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), positions_card, FALSE, FALSE, 0);

    GtkWidget *stats_card = make_card("▤  ACROSS POSITIONS");
    panel->stats_label = gtk_label_new("No completed positions yet.");
    add_class(panel->stats_label, "lnws-muted");
    gtk_widget_set_halign(panel->stats_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(panel->stats_label), TRUE);
    gtk_box_pack_start(GTK_BOX(stats_card), panel->stats_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), stats_card, FALSE, FALSE, 0);

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
    gtk_box_pack_start(GTK_BOX(data_card), panel->scanner_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(data_card), panel->data_entry_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(data_card), panel->data_entry_revealer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), data_card, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(root), spacer, TRUE, TRUE, 0);

    panel->save_button = gtk_button_new_with_label("▣  Save Reading");
    panel->prev_button = gtk_button_new_with_label("←  Previous");
    panel->next_button = gtk_button_new_with_label("→  Next Position");
    panel->complete_button = gtk_button_new_with_label("⚑  Complete Session");
    add_class(panel->save_button, "lnws-action");
    /* Deliberately the smaller icon-action style, not lnws-action: this is
     * a corrective/secondary action (undo an accidental double-click on
     * Next Position), and giving it the same size and weight as Next would
     * just recreate the mis-click risk one button over. */
    add_class(panel->prev_button, "lnws-icon-action");
    add_class(panel->next_button, "lnws-action");
    add_class(panel->complete_button, "lnws-action");
    gtk_box_pack_start(GTK_BOX(root), panel->save_button, FALSE, FALSE, 6);
    GtkWidget *position_nav_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(position_nav_row), panel->prev_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(position_nav_row), panel->next_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), position_nav_row, FALSE, FALSE, 6);
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
    g_signal_connect(panel->operator_button, "clicked", G_CALLBACK(on_operator_button_clicked), panel);
    g_signal_connect(panel->position_combo, "changed", G_CALLBACK(on_position_changed), panel);
    g_signal_connect(panel->qa_standard_combo, "changed", G_CALLBACK(on_qa_standard_changed), panel);
    g_signal_connect(panel->temperature_combo, "changed", G_CALLBACK(on_temperature_changed), panel);
    g_signal_connect(panel->measurement_type_combo, "changed", G_CALLBACK(on_measurement_type_changed), panel);
    g_signal_connect(panel->data_entry_toggle, "clicked", G_CALLBACK(on_toggle_data_entry), panel);
    g_signal_connect(panel->identity_details_toggle, "clicked", G_CALLBACK(on_toggle_identity_details), panel);
    g_signal_connect(panel->open_in_erp_button, "clicked", G_CALLBACK(on_open_in_erp), panel);
    g_signal_connect(panel->erp_button, "clicked", G_CALLBACK(on_erp_settings), panel);
    g_signal_connect(panel->scanner_button, "clicked", G_CALLBACK(on_scanner_settings), panel);

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

    {
        char scanner_config_error[512] = {0};
        int scanner_config_result = ln_scanner_config_load(&panel->scanner_config, scanner_config_error, sizeof(scanner_config_error));
        if (scanner_config_result < 0)
            gtk_label_set_text(GTK_LABEL(panel->status_label), scanner_config_error);
        else if (panel->scanner_config.enabled)
            ln_scanner_start(&panel->scanner_config, on_scanner_scan_received, on_scanner_status, panel);
    }

    /* Wrapped in a scrolled window as a safety net on top of the tighter
     * card spacing/collapsible identity details/grid positions table above
     * -- even after all that, a 1080p screen at certain DPI/window-chrome
     * combinations can still run short a few dozen pixels, and a vertical
     * scrollbar guarantees every field stays reachable rather than
     * silently clipping off the bottom of the window with no way back to
     * it. Horizontal scrolling is disabled -- root's width is fixed above,
     * nothing to scroll sideways for. */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    /* root already has a fixed 390px width request, but that alone doesn't
     * stop THIS widget (the one actually packed into the sidebar revealer)
     * from claiming more horizontal room than that when the main window is
     * resized wider -- a GtkScrolledWindow's own width isn't automatically
     * pinned to its child's fixed width. Locking it here too, and refusing
     * hexpand outright, is what actually keeps the sidebar's left/right
     * edges fixed regardless of window width. Deliberately not touching
     * height/valign here -- that's a separate, still-open problem. */
    gtk_widget_set_size_request(scroll, 390, -1);
    gtk_widget_set_hexpand(scroll, FALSE);
    gtk_container_add(GTK_CONTAINER(scroll), root);

    g_object_set_data_full(G_OBJECT(scroll), "ln-station-panel", panel, g_free);

    /* Connected here, against `scroll`, rather than up above against
     * `root` -- ln_station_panel_advance_position/retreat_position look up
     * the "ln-station-panel" data via g_object_get_data() on whatever
     * widget they're handed, and that data is only ever set on `scroll`
     * (just above), never on `root`. Wiring these against `root` looked
     * fine at a glance but left the lookup returning NULL and both
     * buttons silently doing nothing on click. */
    g_signal_connect(panel->prev_button, "clicked", G_CALLBACK(on_prev_position), scroll);
    g_signal_connect(panel->next_button, "clicked", G_CALLBACK(on_next_position), scroll);

    refresh_position_rows(panel);
    return scroll;
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

    /* A capture can complete without the position ever changing, so the
     * POSITIONS list / completed count / cross-position stats need to
     * refresh here too, not just on the position-change signal path. */
    refresh_position_rows(panel);

    if (status.pending) {
        snprintf(rate_text, sizeof(rate_text), "Rate              repositioning... (%.0fs)", status.pending_seconds_remaining);
        gtk_label_set_text(GTK_LABEL(panel->measurement_rate_label), rate_text);
        gtk_label_set_text(GTK_LABEL(panel->measurement_be_label), "Beat Error        repositioning...");
        gtk_label_set_text(GTK_LABEL(panel->measurement_amp_label), "Amplitude         repositioning...");
        gtk_label_set_text(GTK_LABEL(panel->measurement_bph_label), "BPH               repositioning...");
        if (panel->capture_button)
            gtk_button_set_label(GTK_BUTTON(panel->capture_button), "↻  Recapture");
        return;
    }

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

void ln_station_panel_trigger_capture(GtkWidget *widget) {
    ln_station_capture_begin();
    ln_station_panel_refresh_measurement(widget);
    ln_station_panel_set_status(widget, "Capture started -- keep the watch steady until the hold completes.");
}

static void on_capture_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ln_station_panel_trigger_capture(GTK_WIDGET(user_data));
}

void ln_station_panel_bind_capture(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (!panel || !panel->capture_button) return;
    g_signal_connect(panel->capture_button, "clicked", G_CALLBACK(on_capture_clicked), widget);
}

/* Shared by Complete Session and "the identity just changed to something
 * new" (see lookup_identity_now) -- both mean whatever was on the bench
 * before is gone, so position results, capture state, and the identity-
 * derived display fields must not carry over. clear_identity_entry is only
 * true for Complete Session: when the identity field itself just changed,
 * it already holds the new (correct) value and must not be wiped out from
 * under the operator who just typed/scanned it. */
static void reset_session_state(LnStationPanel *panel, int clear_identity_entry) {
    if (!panel) return;

    if (clear_identity_entry) {
        panel->suppress_identity_change = 1;
        gtk_entry_set_text(GTK_ENTRY(panel->identity_entry), "");
        panel->suppress_identity_change = 0;
        ln_station_set_identity(panel->ctx, "");
        panel->last_confirmed_identity[0] = '\0';
    }
    set_readonly_entry(panel->movement_serial_entry, "");
    set_readonly_entry(panel->movement_part_entry, "");
    set_readonly_entry(panel->watch_unit_entry, "");
    set_readonly_entry(panel->work_order_entry, "");
    set_readonly_entry(panel->identity_status_entry, "");
    panel->ctx->work_order[0] = '\0';

    /* Reset to the first position. gtk_combo_box_set_active() only emits
     * "changed" (which would otherwise drive this itself via
     * on_position_changed) when the index actually moves, so the
     * underlying position/capture reset is also called explicitly here to
     * guarantee it happens even if the combo was already showing the first
     * position. Calling it twice in the "index did move" case is harmless
     * -- it's fully idempotent. */
    if (panel->position_combo)
        gtk_combo_box_set_active(GTK_COMBO_BOX(panel->position_combo), 0);
    ln_station_set_position(panel->ctx, LN_POSITIONS[0]);

    ln_station_position_results_reset();
    refresh_position_rows(panel);
    if (panel->root)
        ln_station_panel_refresh_measurement(panel->root);
}

void ln_station_panel_refresh_operator(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    if (panel) refresh_operator_widgets(panel);
}

int ln_station_panel_prompt_operator_sign_in(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    int signed_in;
    if (!panel) return 0;
    if (ln_station_operator_signed_in(panel->ctx)) {
        refresh_operator_widgets(panel);
        return 1;
    }
    signed_in = run_operator_sign_in_dialog(panel);
    refresh_operator_widgets(panel);
    return signed_in;
}

void ln_station_panel_reset_session(GtkWidget *widget) {
    LnStationPanel *panel = g_object_get_data(G_OBJECT(widget), "ln-station-panel");
    reset_session_state(panel, 1);
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
