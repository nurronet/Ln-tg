#include "ln_scanner.h"

#include <windows.h>
#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LN_SCANNER_CONFIG_DIR "ln-watchmaker-station"
#define LN_SCANNER_CONFIG_FILE "scanner.ini"
#define LN_SCANNER_READ_BUF_MAX 256

static char config_path[1024];

static GThread *g_scanner_thread = NULL;
static volatile gint g_scanner_stop_flag = 0;
static HANDLE g_scanner_port_handle = INVALID_HANDLE_VALUE;
static LnScannerScanCallback g_scanner_on_scan = NULL;
static LnScannerStatusCallback g_scanner_on_status = NULL;
static gpointer g_scanner_user_data = NULL;

static void safe_copy(char *dst, unsigned long dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static void set_error(char *dst, unsigned long len, const char *message) {
    safe_copy(dst, len, message ? message : "Unknown error");
}

void ln_scanner_config_init(LnScannerConfig *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->enabled = 0;
    config->baud_rate = 9600;
}

const char *ln_scanner_config_file_path(void) {
    const char *base = g_get_user_config_dir();
    g_snprintf(config_path, sizeof(config_path), "%s%c%s%c%s",
               base, G_DIR_SEPARATOR, LN_SCANNER_CONFIG_DIR, G_DIR_SEPARATOR, LN_SCANNER_CONFIG_FILE);
    return config_path;
}

int ln_scanner_config_load(LnScannerConfig *config, char *error_text, unsigned long error_text_len) {
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *value;
    const char *path;

    if (!config) return -1;
    ln_scanner_config_init(config);
    path = ln_scanner_config_file_path();

    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return 1;

    key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        set_error(error_text, error_text_len, error ? error->message : "Unable to load scanner settings");
        if (error) g_error_free(error);
        g_key_file_free(key_file);
        return -2;
    }

    value = g_key_file_get_string(key_file, "scanner", "port", NULL);
    if (value) { safe_copy(config->port, sizeof(config->port), value); g_free(value); }

    config->enabled = g_key_file_get_boolean(key_file, "scanner", "enabled", NULL);
    {
        GError *baud_error = NULL;
        int baud = g_key_file_get_integer(key_file, "scanner", "baud_rate", &baud_error);
        if (!baud_error && baud > 0) config->baud_rate = baud;
        if (baud_error) g_error_free(baud_error);
    }

    g_key_file_free(key_file);
    return 0;
}

int ln_scanner_config_save(const LnScannerConfig *config, char *error_text, unsigned long error_text_len) {
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *data;
    gsize data_len;
    gchar *directory;
    const char *path;

    if (!config) return -1;
    path = ln_scanner_config_file_path();
    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        set_error(error_text, error_text_len, "Unable to create LN Watchmaker Station configuration directory");
        g_free(directory);
        return -2;
    }
    g_free(directory);

    key_file = g_key_file_new();
    g_key_file_set_string(key_file, "scanner", "port", config->port);
    g_key_file_set_integer(key_file, "scanner", "baud_rate", config->baud_rate);
    g_key_file_set_boolean(key_file, "scanner", "enabled", config->enabled);

    data = g_key_file_to_data(key_file, &data_len, &error);
    if (!data || error) {
        set_error(error_text, error_text_len, error ? error->message : "Unable to serialize scanner settings");
        if (error) g_error_free(error);
        g_key_file_free(key_file);
        return -3;
    }

    if (!g_file_set_contents(path, data, (gssize)data_len, &error)) {
        set_error(error_text, error_text_len, error ? error->message : "Unable to save scanner settings");
        if (error) g_error_free(error);
        g_free(data);
        g_key_file_free(key_file);
        return -4;
    }

    g_free(data);
    g_key_file_free(key_file);
    return 0;
}

typedef struct {
    char *value;
    LnScannerScanCallback on_scan;
    gpointer user_data;
} ScanIdleData;

static gboolean deliver_scan(gpointer data) {
    ScanIdleData *d = (ScanIdleData *)data;
    if (d->on_scan) d->on_scan(d->value, d->user_data);
    g_free(d->value);
    g_free(d);
    return G_SOURCE_REMOVE;
}

typedef struct {
    char *message;
    LnScannerStatusCallback on_status;
    gpointer user_data;
} StatusIdleData;

static gboolean deliver_status(gpointer data) {
    StatusIdleData *d = (StatusIdleData *)data;
    if (d->on_status) d->on_status(d->message, d->user_data);
    g_free(d->message);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void post_status(const char *fmt, ...) {
    char message[512];
    va_list args;
    StatusIdleData *d;

    if (!g_scanner_on_status) return;
    va_start(args, fmt);
    g_vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    fprintf(stderr, "[LNWS SCANNER] %s\n", message);
    fflush(stderr);

    d = g_new0(StatusIdleData, 1);
    d->message = g_strdup(message);
    d->on_status = g_scanner_on_status;
    d->user_data = g_scanner_user_data;
    g_idle_add(deliver_status, d);
}

/* Runs entirely on the background thread -- opens the port, reads bytes one
 * at a time, and treats a CR or LF as the end of one scanned value (a
 * following LF right after a CR, or vice versa, is naturally absorbed since
 * it lands on an empty buffer and is just dropped rather than emitted as a
 * second, empty scan). Every scan is handed to the main thread via
 * g_idle_add -- this thread must never touch a GTK widget directly. */
static gpointer scanner_thread_func(gpointer param) {
    LnScannerConfig *config = (LnScannerConfig *)param;
    char com_path[64];
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS timeouts;
    char read_buf[LN_SCANNER_READ_BUF_MAX];
    size_t read_len = 0;
    char byte;
    DWORD bytes_read;

    g_snprintf(com_path, sizeof(com_path), "\\\\.\\%s", config->port);
    h = CreateFileA(com_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        post_status("Failed to open %s (error %lu) -- check the port name and that no other "
                     "application has it open.", config->port, (unsigned long)GetLastError());
        g_free(config);
        return NULL;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        post_status("Failed to read current state of %s (error %lu).", config->port, (unsigned long)GetLastError());
        CloseHandle(h);
        g_free(config);
        return NULL;
    }
    dcb.BaudRate = (DWORD)config->baud_rate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) {
        post_status("Failed to configure %s at %d baud (error %lu) -- the port may not support "
                     "this baud rate.", config->port, config->baud_rate, (unsigned long)GetLastError());
        CloseHandle(h);
        g_free(config);
        return NULL;
    }

    /* Short timeouts so the read loop wakes up regularly to check the stop
     * flag, rather than blocking indefinitely on an idle port. */
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 200;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &timeouts);

    g_scanner_port_handle = h;
    post_status("Connected to %s at %d baud.", config->port, config->baud_rate);

    while (!g_atomic_int_get(&g_scanner_stop_flag)) {
        if (!ReadFile(h, &byte, 1, &bytes_read, NULL)) {
            /* CancelIoEx (from ln_scanner_stop) surfaces as a read error --
             * treat any failure here as "stop", not a reason to keep retrying. */
            break;
        }
        if (bytes_read != 1) continue; /* timed out with nothing to read */

        if (byte == '\r' || byte == '\n') {
            if (read_len > 0) {
                ScanIdleData *d;
                read_buf[read_len] = '\0';
                d = g_new0(ScanIdleData, 1);
                d->value = g_strdup(read_buf);
                d->on_scan = g_scanner_on_scan;
                d->user_data = g_scanner_user_data;
                g_idle_add(deliver_scan, d);
                read_len = 0;
            }
        } else if (read_len < sizeof(read_buf) - 1) {
            read_buf[read_len++] = byte;
        }
        /* A line longer than the buffer is silently truncated rather than
         * grown -- no real barcode payload approaches this length, and an
         * unbounded buffer on a wedged/noisy port is worse than truncation. */
    }

    g_scanner_port_handle = INVALID_HANDLE_VALUE;
    CloseHandle(h);
    post_status("Disconnected from %s.", config->port);
    g_free(config);
    return NULL;
}

void ln_scanner_start(const LnScannerConfig *config, LnScannerScanCallback on_scan,
                       LnScannerStatusCallback on_status, gpointer user_data) {
    LnScannerConfig *config_copy;

    /* Always stop any existing thread first, even if the new config is
     * disabled or incomplete -- otherwise turning scanning off and saving
     * would leave a previous session's thread running against stale
     * settings instead of actually stopping it. */
    ln_scanner_stop();

    if (!config || !config->enabled || !config->port[0]) return;

    g_scanner_on_scan = on_scan;
    g_scanner_on_status = on_status;
    g_scanner_user_data = user_data;
    g_atomic_int_set(&g_scanner_stop_flag, 0);

    config_copy = g_new0(LnScannerConfig, 1);
    *config_copy = *config;
    g_scanner_thread = g_thread_new("ln-scanner", scanner_thread_func, config_copy);
}

void ln_scanner_stop(void) {
    if (!g_scanner_thread) return;
    g_atomic_int_set(&g_scanner_stop_flag, 1);
    /* Unblocks a pending ReadFile immediately rather than waiting out the
     * (short, but non-zero) read timeout. */
    if (g_scanner_port_handle != INVALID_HANDLE_VALUE)
        CancelIoEx(g_scanner_port_handle, NULL);
    g_thread_join(g_scanner_thread);
    g_scanner_thread = NULL;
}

gboolean ln_scanner_is_running(void) {
    return g_scanner_thread != NULL;
}

static GtkWidget *labeled_entry(GtkWidget *grid, int row, const char *label_text, const char *value) {
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
    return entry;
}

int ln_scanner_dialog_run(GtkWindow *parent, LnScannerConfig *config, char *status_text, unsigned long status_text_len) {
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *port_entry;
    GtkWidget *baud_entry;
    GtkWidget *enabled_check;
    GtkWidget *status_label;
    char baud_text[16];
    int response;
    int saved = 0;

    if (!config) return 0;

    dialog = gtk_dialog_new_with_buttons(
        "Scanner Connection (USB COM)",
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        NULL
    );
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, 260);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 14);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    g_snprintf(baud_text, sizeof(baud_text), "%d", config->baud_rate > 0 ? config->baud_rate : 9600);
    port_entry = labeled_entry(grid, 0, "COM Port (e.g. COM7)", config->port);
    baud_entry = labeled_entry(grid, 1, "Baud Rate", baud_text);

    enabled_check = gtk_check_button_new_with_label("Read identity scans from this COM port");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled_check), config->enabled);
    gtk_grid_attach(GTK_GRID(grid), enabled_check, 1, 2, 1, 1);

    status_label = gtk_label_new(
        "Use the scanner's \"USB COM\" mode, not \"USB\" (keyboard) mode. Check Windows Device "
        "Manager -> Ports (COM & LPT) for the port name if you're not sure. 9600 baud covers most "
        "scanners -- check the scanner's manual/config barcode if scans come through empty.");
    gtk_label_set_line_wrap(GTK_LABEL(status_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(status_label), 60);
    gtk_widget_set_halign(status_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), status_label, 0, 3, 2, 1);

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        safe_copy(config->port, sizeof(config->port), gtk_entry_get_text(GTK_ENTRY(port_entry)));
        config->baud_rate = atoi(gtk_entry_get_text(GTK_ENTRY(baud_entry)));
        if (config->baud_rate <= 0) config->baud_rate = 9600;
        config->enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled_check));

        if (ln_scanner_config_save(config, status_text, status_text_len) == 0)
            saved = 1;
    }

    gtk_widget_destroy(dialog);
    return saved;
}
