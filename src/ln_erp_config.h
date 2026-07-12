#ifndef LN_ERP_CONFIG_H
#define LN_ERP_CONFIG_H

#include <gtk/gtk.h>
#include "ln_station.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char base_url[256];
    char api_key[256];
    char api_secret[256];
    char station_id[64];
    char queue_dir[512];
    int enabled;
    int auto_lookup;
    int verify_tls;
} LnErpConfig;

void ln_erp_config_init(LnErpConfig *config);
int ln_erp_config_load(LnErpConfig *config, char *error_text, unsigned long error_text_len);
int ln_erp_config_save(const LnErpConfig *config, char *error_text, unsigned long error_text_len);
void ln_erp_config_apply_to_station(const LnErpConfig *config, LnStationContext *ctx);
int ln_erp_config_test_connection(const LnErpConfig *config, char *response_text, unsigned long response_text_len);
int ln_erp_identity_search(const LnErpConfig *config, const char *query, char *response_text, unsigned long response_text_len);
int ln_erp_identity_lookup(const LnErpConfig *config, const char *identity_id, char *response_text, unsigned long response_text_len);
int ln_erp_config_dialog_run(GtkWindow *parent, LnErpConfig *config, char *status_text, unsigned long status_text_len);
const char *ln_erp_config_file_path(void);

#ifdef __cplusplus
}
#endif

#endif
