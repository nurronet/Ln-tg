#include "ln_erp_config.h"

#include <curl/curl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LN_ERP_CONFIG_DIR "ln-watchmaker-station"
#define LN_ERP_CONFIG_FILE "erp.ini"

static char config_path[1024];

static void safe_copy(char *dst, unsigned long dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static void set_error(char *dst, unsigned long len, const char *message) {
    safe_copy(dst, len, message ? message : "Unknown error");
}

static void ln_erp_debug(const char *operation, const char *detail) {
    fprintf(stderr, "[LNWS ERP] %s: %s\n",
            operation ? operation : "request",
            detail ? detail : "");
    fflush(stderr);
}

static void normalize_base_url(char *url, unsigned long len) {
    size_t n;
    if (!url || !*url) return;
    n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        url[n - 1] = '\0';
        n--;
    }
    (void)len;
}

const char *ln_erp_config_file_path(void) {
    const char *base = g_get_user_config_dir();
    g_snprintf(config_path, sizeof(config_path), "%s%c%s%c%s",
               base, G_DIR_SEPARATOR, LN_ERP_CONFIG_DIR, G_DIR_SEPARATOR, LN_ERP_CONFIG_FILE);
    return config_path;
}

void ln_erp_config_init(LnErpConfig *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    safe_copy(config->station_id, sizeof(config->station_id), "LN-TG-001");
    safe_copy(config->queue_dir, sizeof(config->queue_dir), "ln_exports");
    config->enabled = 0;
    config->auto_lookup = 1;
    config->verify_tls = 1;
}

int ln_erp_config_load(LnErpConfig *config, char *error_text, unsigned long error_text_len) {
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *value;
    const char *path;

    if (!config) return -1;
    ln_erp_config_init(config);
    path = ln_erp_config_file_path();

    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return 1;

    key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        set_error(error_text, error_text_len, error ? error->message : "Unable to load ERP settings");
        if (error) g_error_free(error);
        g_key_file_free(key_file);
        return -2;
    }

    value = g_key_file_get_string(key_file, "erp", "base_url", NULL);
    if (value) { safe_copy(config->base_url, sizeof(config->base_url), value); g_free(value); }
    value = g_key_file_get_string(key_file, "erp", "api_key", NULL);
    if (value) { safe_copy(config->api_key, sizeof(config->api_key), value); g_free(value); }
    value = g_key_file_get_string(key_file, "erp", "api_secret", NULL);
    if (value) { safe_copy(config->api_secret, sizeof(config->api_secret), value); g_free(value); }
    value = g_key_file_get_string(key_file, "erp", "station_id", NULL);
    if (value) { safe_copy(config->station_id, sizeof(config->station_id), value); g_free(value); }
    value = g_key_file_get_string(key_file, "erp", "queue_dir", NULL);
    if (value) { safe_copy(config->queue_dir, sizeof(config->queue_dir), value); g_free(value); }

    config->enabled = g_key_file_get_boolean(key_file, "erp", "enabled", NULL);
    config->auto_lookup = g_key_file_get_boolean(key_file, "erp", "auto_lookup", NULL);
    config->verify_tls = g_key_file_get_boolean(key_file, "erp", "verify_tls", NULL);

    normalize_base_url(config->base_url, sizeof(config->base_url));
    g_key_file_free(key_file);
    return 0;
}

int ln_erp_config_save(const LnErpConfig *config, char *error_text, unsigned long error_text_len) {
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *data;
    gsize data_len;
    gchar *directory;
    const char *path;

    if (!config) return -1;
    path = ln_erp_config_file_path();
    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        set_error(error_text, error_text_len, "Unable to create LN Watchmaker Station configuration directory");
        g_free(directory);
        return -2;
    }
    g_free(directory);

    key_file = g_key_file_new();
    g_key_file_set_string(key_file, "erp", "base_url", config->base_url);
    g_key_file_set_string(key_file, "erp", "api_key", config->api_key);
    g_key_file_set_string(key_file, "erp", "api_secret", config->api_secret);
    g_key_file_set_string(key_file, "erp", "station_id", config->station_id);
    g_key_file_set_string(key_file, "erp", "queue_dir", config->queue_dir);
    g_key_file_set_boolean(key_file, "erp", "enabled", config->enabled);
    g_key_file_set_boolean(key_file, "erp", "auto_lookup", config->auto_lookup);
    g_key_file_set_boolean(key_file, "erp", "verify_tls", config->verify_tls);

    data = g_key_file_to_data(key_file, &data_len, &error);
    if (!data || error) {
        set_error(error_text, error_text_len, error ? error->message : "Unable to serialize ERP settings");
        if (error) g_error_free(error);
        g_key_file_free(key_file);
        return -3;
    }

    if (!g_file_set_contents(path, data, (gssize)data_len, &error)) {
        set_error(error_text, error_text_len, error ? error->message : "Unable to save ERP settings");
        if (error) g_error_free(error);
        g_free(data);
        g_key_file_free(key_file);
        return -4;
    }

    g_free(data);
    g_key_file_free(key_file);
    return 0;
}

void ln_erp_config_apply_to_station(const LnErpConfig *config, LnStationContext *ctx) {
    if (!config || !ctx) return;
    safe_copy(ctx->erp_base_url, sizeof(ctx->erp_base_url), config->base_url);
    safe_copy(ctx->api_key, sizeof(ctx->api_key), config->api_key);
    safe_copy(ctx->api_secret, sizeof(ctx->api_secret), config->api_secret);
    safe_copy(ctx->station_id, sizeof(ctx->station_id), config->station_id);
    safe_copy(ctx->export_dir, sizeof(ctx->export_dir), config->queue_dir);
    ctx->submit_enabled = config->enabled ? true : false;
    ctx->verify_tls = config->verify_tls ? true : false;
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} ResponseBuffer;

static size_t response_write(void *contents, size_t size, size_t nmemb, void *user_data) {
    size_t bytes = size * nmemb;
    ResponseBuffer *response = (ResponseBuffer *)user_data;
    size_t remaining;
    size_t copy_len;

    if (!response || !response->buffer || response->capacity == 0) return bytes;
    remaining = response->capacity - response->length - 1;
    copy_len = bytes < remaining ? bytes : remaining;
    if (copy_len > 0) {
        memcpy(response->buffer + response->length, contents, copy_len);
        response->length += copy_len;
        response->buffer[response->length] = '\0';
    }
    return bytes;
}

int ln_erp_config_test_connection(const LnErpConfig *config, char *response_text, unsigned long response_text_len) {
    CURL *curl;
    CURLcode result;
    struct curl_slist *headers = NULL;
    char url[768];
    char auth[640];
    char station_encoded[256];
    char *escaped_station;
    long status = 0;
    double total_seconds = 0.0;
    double connect_seconds = 0.0;
    ResponseBuffer response;

    if (!config || !config->base_url[0] || !config->api_key[0] || !config->api_secret[0]) {
        set_error(response_text, response_text_len, "ERP URL, API key, and API secret are required");
        return -1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        set_error(response_text, response_text_len, "Unable to initialize HTTP client");
        return -2;
    }

    escaped_station = curl_easy_escape(curl, config->station_id, 0);
    safe_copy(station_encoded, sizeof(station_encoded), escaped_station ? escaped_station : config->station_id);
    if (escaped_station) curl_free(escaped_station);

    g_snprintf(url, sizeof(url), "%s/api/method/ln_watch_inventory.freeerp.ping?station_id=%s&software_version=0.9.1",
               config->base_url, station_encoded);
    g_snprintf(auth, sizeof(auth), "Authorization: token %s:%s", config->api_key, config->api_secret);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: LN-Watchmaker-Station/0.9");

    response.buffer = response_text;
    response.capacity = response_text_len;
    response.length = 0;
    if (response_text && response_text_len) response_text[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config->verify_tls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config->verify_tls ? 2L : 0L);

    result = curl_easy_perform(curl);
    if (result == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        set_error(response_text, response_text_len, curl_easy_strerror(result));
        return -3;
    }
    if (status < 200 || status >= 300) {
        char body[1536];
        char message[2048];
        safe_copy(body, sizeof(body), response_text && response_text[0] ? response_text : "No response body");
        g_snprintf(message, sizeof(message), "ERP returned HTTP %ld\n%s", status, body);
        set_error(response_text, response_text_len, message);
        return (int)status;
    }

    if (!response_text || !response_text[0])
        set_error(response_text, response_text_len, "Connected successfully");
    return 0;
}


static int ln_erp_authenticated_get(const LnErpConfig *config, const char *method_path,
                                    const char *query_string, char *response_text,
                                    unsigned long response_text_len) {
    CURL *curl;
    CURLcode result;
    struct curl_slist *headers = NULL;
    char url[1536];
    char auth[640];
    long status = 0;
    double total_seconds = 0.0;
    double connect_seconds = 0.0;
    ResponseBuffer response;

    if (!config || !config->enabled || !config->base_url[0] ||
        !config->api_key[0] || !config->api_secret[0]) {
        set_error(response_text, response_text_len, "ERP synchronization is not configured or enabled");
        return -1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        set_error(response_text, response_text_len, "Unable to initialize HTTP client");
        return -2;
    }

    g_snprintf(url, sizeof(url), "%s/api/method/%s%s%s",
               config->base_url, method_path,
               query_string && *query_string ? "?" : "",
               query_string && *query_string ? query_string : "");
    g_snprintf(auth, sizeof(auth), "Authorization: token %s:%s", config->api_key, config->api_secret);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: LN-Watchmaker-Station/0.10");

    response.buffer = response_text;
    response.capacity = response_text_len;
    response.length = 0;
    if (response_text && response_text_len) response_text[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config->verify_tls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config->verify_tls ? 2L : 0L);

    ln_erp_debug("GET", url);
    result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_seconds);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_seconds);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        char message[1024];
        g_snprintf(message, sizeof(message),
                   "Network error: %s (connect timeout 5s, total timeout 10s)",
                   curl_easy_strerror(result));
        ln_erp_debug("ERROR", message);
        set_error(response_text, response_text_len, message);
        return result == CURLE_OPERATION_TIMEDOUT ? -408 : -3;
    }
    if (status < 200 || status >= 300) {
        char body[1200];
        char message[1800];
        safe_copy(body, sizeof(body), response_text && response_text[0] ? response_text : "No response body");
        g_snprintf(message, sizeof(message),
                   "HTTP %ld after %.0f ms (connect %.0f ms)\n%s",
                   status, total_seconds * 1000.0, connect_seconds * 1000.0, body);
        ln_erp_debug("HTTP ERROR", message);
        set_error(response_text, response_text_len, message);
        return (int)status;
    }

    {
        char message[256];
        g_snprintf(message, sizeof(message),
                   "HTTP %ld in %.0f ms (connect %.0f ms, %lu bytes)",
                   status, total_seconds * 1000.0, connect_seconds * 1000.0,
                   response.length);
        ln_erp_debug("SUCCESS", message);
    }
    return 0;
}

int ln_erp_identity_search(const LnErpConfig *config, const char *query,
                           char *response_text, unsigned long response_text_len) {
    CURL *curl;
    char *escaped_query;
    char *escaped_station;
    char query_string[768];
    int rc;

    curl = curl_easy_init();
    if (!curl) return -2;
    escaped_query = curl_easy_escape(curl, query ? query : "", 0);
    escaped_station = curl_easy_escape(curl, config && config->station_id[0] ? config->station_id : "", 0);
    g_snprintf(query_string, sizeof(query_string), "query=%s&limit=12&station_id=%s",
               escaped_query ? escaped_query : "", escaped_station ? escaped_station : "");
    if (escaped_query) curl_free(escaped_query);
    if (escaped_station) curl_free(escaped_station);
    curl_easy_cleanup(curl);

    rc = ln_erp_authenticated_get(config, "ln_watch_inventory.freeerp.search_identities",
                                  query_string, response_text, response_text_len);
    return rc;
}

int ln_erp_identity_lookup(const LnErpConfig *config, const char *identity_id,
                           char *response_text, unsigned long response_text_len) {
    CURL *curl;
    char *escaped_identity;
    char *escaped_station;
    char query_string[768];
    int rc;

    curl = curl_easy_init();
    if (!curl) return -2;
    escaped_identity = curl_easy_escape(curl, identity_id ? identity_id : "", 0);
    escaped_station = curl_easy_escape(curl, config && config->station_id[0] ? config->station_id : "", 0);
    g_snprintf(query_string, sizeof(query_string), "identity_id=%s&station_id=%s",
               escaped_identity ? escaped_identity : "", escaped_station ? escaped_station : "");
    if (escaped_identity) curl_free(escaped_identity);
    if (escaped_station) curl_free(escaped_station);
    curl_easy_cleanup(curl);

    rc = ln_erp_authenticated_get(config, "ln_watch_inventory.freeerp.lookup_identity",
                                  query_string, response_text, response_text_len);
    return rc;
}

static GtkWidget *labeled_entry(GtkWidget *grid, int row, const char *label_text, const char *value, int password) {
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
    if (password) {
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        gtk_entry_set_invisible_char(GTK_ENTRY(entry), 0x2022);
    }
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
    return entry;
}

int ln_erp_config_dialog_run(GtkWindow *parent, LnErpConfig *config, char *status_text, unsigned long status_text_len) {
    enum { LN_RESPONSE_TEST = 1001 };
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *url_entry;
    GtkWidget *key_entry;
    GtkWidget *secret_entry;
    GtkWidget *station_entry;
    GtkWidget *queue_entry;
    GtkWidget *enabled_check;
    GtkWidget *lookup_check;
    GtkWidget *tls_check;
    GtkWidget *status_label;
    int response;
    int saved = 0;

    if (!config) return 0;

    dialog = gtk_dialog_new_with_buttons(
        "ERP Connection",
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Test Connection", LN_RESPONSE_TEST,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        NULL
    );
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 430);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 14);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    url_entry = labeled_entry(grid, 0, "ERP Base URL", config->base_url, 0);
    key_entry = labeled_entry(grid, 1, "API Key", config->api_key, 0);
    secret_entry = labeled_entry(grid, 2, "API Secret", config->api_secret, 1);
    station_entry = labeled_entry(grid, 3, "Station ID", config->station_id, 0);
    queue_entry = labeled_entry(grid, 4, "Offline Queue", config->queue_dir, 0);

    enabled_check = gtk_check_button_new_with_label("Enable ERP synchronization");
    lookup_check = gtk_check_button_new_with_label("Look up identity automatically after scan");
    tls_check = gtk_check_button_new_with_label("Verify TLS certificate");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled_check), config->enabled);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lookup_check), config->auto_lookup);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tls_check), config->verify_tls);
    gtk_grid_attach(GTK_GRID(grid), enabled_check, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lookup_check, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), tls_check, 1, 7, 1, 1);

    status_label = gtk_label_new("Settings are stored in the current user's configuration directory.");
    gtk_label_set_line_wrap(GTK_LABEL(status_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(status_label), 78);
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), status_label, 0, 8, 2, 1);

    gtk_widget_show_all(dialog);

    for (;;) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));

        safe_copy(config->base_url, sizeof(config->base_url), gtk_entry_get_text(GTK_ENTRY(url_entry)));
        safe_copy(config->api_key, sizeof(config->api_key), gtk_entry_get_text(GTK_ENTRY(key_entry)));
        safe_copy(config->api_secret, sizeof(config->api_secret), gtk_entry_get_text(GTK_ENTRY(secret_entry)));
        safe_copy(config->station_id, sizeof(config->station_id), gtk_entry_get_text(GTK_ENTRY(station_entry)));
        safe_copy(config->queue_dir, sizeof(config->queue_dir), gtk_entry_get_text(GTK_ENTRY(queue_entry)));
        normalize_base_url(config->base_url, sizeof(config->base_url));
        config->enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled_check));
        config->auto_lookup = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lookup_check));
        config->verify_tls = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tls_check));

        if (response == LN_RESPONSE_TEST) {
            char test_response[2048];
            int rc = ln_erp_config_test_connection(config, test_response, sizeof(test_response));
            if (rc == 0) {
                char message[2300];
                g_snprintf(message, sizeof(message), "Connected successfully.\n%s", test_response[0] ? test_response : "FreeERP ping returned no body.");
                gtk_label_set_text(GTK_LABEL(status_label), message);
            } else {
                char message[2300];
                g_snprintf(message, sizeof(message), "Connection failed.\n%s", test_response);
                gtk_label_set_text(GTK_LABEL(status_label), message);
            }
            continue;
        }

        if (response == GTK_RESPONSE_ACCEPT) {
            if (ln_erp_config_save(config, status_text, status_text_len) == 0) {
                saved = 1;
                break;
            }
            gtk_label_set_text(GTK_LABEL(status_label), status_text);
            continue;
        }

        break;
    }

    gtk_widget_destroy(dialog);
    return saved;
}
