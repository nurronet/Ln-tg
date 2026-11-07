#ifndef LN_SCANNER_H
#define LN_SCANNER_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int enabled;
    char port[32];      /* e.g. "COM7" */
    int baud_rate;       /* e.g. 9600 -- the near-universal default for USB-COM
                           * scanner emulation, but check the scanner's own
                           * manual/config barcode if scans come through empty
                           * or garbled. */
} LnScannerConfig;

/* Called on the GTK main thread (marshalled off the background reader
 * thread via g_idle_add) with one complete scanned value, CR/LF-terminated
 * bytes already stripped. */
typedef void (*LnScannerScanCallback)(const char *value, gpointer user_data);

/* Called on the GTK main thread for connection status/error messages (e.g.
 * "Connected to COM7", "Failed to open COM7: ..."). Same shape as the
 * ERP status label updates elsewhere in this app. */
typedef void (*LnScannerStatusCallback)(const char *message, gpointer user_data);

void ln_scanner_config_init(LnScannerConfig *config);
int ln_scanner_config_load(LnScannerConfig *config, char *error_text, unsigned long error_text_len);
int ln_scanner_config_save(const LnScannerConfig *config, char *error_text, unsigned long error_text_len);
const char *ln_scanner_config_file_path(void);

/* Starts (or, if already running, restarts with new settings) the background
 * COM-port reader thread. No-op if config->enabled is false. Scanned values
 * and status messages are delivered via the callbacks on the GTK main
 * thread, never from the background thread directly -- callers must not
 * touch GTK widgets from anywhere else. */
void ln_scanner_start(const LnScannerConfig *config, LnScannerScanCallback on_scan,
                       LnScannerStatusCallback on_status, gpointer user_data);
/* Stops the background thread if running and closes the port. Safe to call
 * even if never started. Blocks briefly (typically well under the read
 * timeout) waiting for the thread to actually exit. */
void ln_scanner_stop(void);
gboolean ln_scanner_is_running(void);

int ln_scanner_dialog_run(GtkWindow *parent, LnScannerConfig *config, char *status_text, unsigned long status_text_len);

#ifdef __cplusplus
}
#endif

#endif
