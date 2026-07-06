#ifndef LN_STATION_H
#define LN_STATION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char station_id[64];
    char operator_id[64];
    char identity_id[128];
    char work_order[128];
    char position[32];
    char erp_base_url[256];
    char api_key[256];
    char api_secret[256];
    bool submit_enabled;
    bool export_enabled;
    char export_dir[512];
} LnStationContext;

typedef struct {
    double rate_s_per_day;
    double beat_error_ms;
    double amplitude_deg;
    double beat_frequency_bph;
    double lift_angle_deg;
    double sample_rate_hz;
    double duration_seconds;
    char timestamp_iso[64];
    char notes[512];
} LnTimingResult;

void ln_station_init(LnStationContext *ctx);
void ln_station_set_identity(LnStationContext *ctx, const char *identity_id);
void ln_station_set_operator(LnStationContext *ctx, const char *operator_id);
void ln_station_set_position(LnStationContext *ctx, const char *position);

int ln_station_export_json(const LnStationContext *ctx, const LnTimingResult *result, char *out_path, unsigned long out_path_len);
int ln_station_submit_result(const LnStationContext *ctx, const LnTimingResult *result, char *response, unsigned long response_len);

#ifdef __cplusplus
}
#endif

#endif
