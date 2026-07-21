#ifndef LN_STATION_H
#define LN_STATION_H

#include <stdbool.h>

#ifndef LN_STATION_VERSION
#define LN_STATION_VERSION "0.9.0"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char station_id[64];
    char operator_id[64];
    char identity_id[128];
    char work_order[128];
    char position[32];
    char qa_standard[32];
    char temperature_condition[32];
    char measurement_type[48];
    bool followup_mode;
    char erp_base_url[256];
    char api_key[256];
    char api_secret[256];
    bool submit_enabled;
    bool export_enabled;
    bool verify_tls;
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
    double baseline_rate_s_per_day;
    double baseline_beat_error_ms;
    double baseline_amplitude_deg;
    bool has_baseline;
    char timestamp_iso[64];
    char notes[512];
} LnTimingResult;

typedef struct {
    char key_id[96];
    char signer_type[32];
    char signature_b64[128];
} LnMeasurementSignature;

/* Status of the gated sample-capture window shown in the LN station
 * panel's MEASUREMENT card. Separate from (and does not affect) the
 * always-on rate-only hold indicator driven by ln_station_qa_update_rate
 * -- that one feeds the top-bar "pos ... +-Ns / check" readout and keeps
 * running regardless of whether a capture has been started. This one
 * only accumulates once ln_station_capture_begin() has been called (via
 * the panel's Start Capture button, or automatically on a position
 * change once auto mode has latched), logs every valid sample taken
 * during the hold window, and reports a running average. */
typedef struct {
    int active;
    int pending;                        /* waiting out the reposition delay before auto-arming */
    double pending_seconds_remaining;
    int complete;
    double elapsed_seconds;
    int required_seconds;
    int sample_count;
    double avg_rate_s_per_day;
    double avg_beat_error_ms;
    double avg_amplitude_deg;
    double avg_bph;
} LnCaptureStatus;

void ln_station_init(LnStationContext *ctx);
void ln_station_set_identity(LnStationContext *ctx, const char *identity_id);
void ln_station_set_operator(LnStationContext *ctx, const char *operator_id);
void ln_station_set_position(LnStationContext *ctx, const char *position);
const char *ln_station_current_position(void);
void ln_station_set_qa_standard(const char *standard);
void ln_station_set_temperature_condition(const char *temperature_condition);
const char *ln_station_current_temperature_condition(void);
void ln_station_set_measurement_type(const char *measurement_type);
const char *ln_station_current_measurement_type(void);
void ln_station_set_followup_mode(int followup_mode);
int ln_station_is_followup(void);
void ln_station_set_baseline_for_position(const char *position, double rate_s_per_day, double beat_error_ms, double amplitude_deg);
int ln_station_get_baseline_for_current_position(double *rate_s_per_day, double *beat_error_ms, double *amplitude_deg);
const char *ln_station_current_qa_standard(void);
int ln_station_current_qa_rate_limit(void);
void ln_station_qa_update_rate(double rate_s_per_day, int valid);
int ln_station_position_qa_passed(void);
double ln_station_position_qa_seconds(void);

/* Sample-capture window (see LnCaptureStatus above). Duration is looked
 * up per QA standard (set via ln_station_set_qa_standard) rather than a
 * single fixed constant -- stricter certification tiers hold for longer. */
void ln_station_capture_begin(void);
void ln_station_capture_feed(double rate_s_per_day, double beat_error_ms, double amplitude_deg, double bph, int valid);
void ln_station_capture_status(LnCaptureStatus *out);
int ln_station_capture_is_auto(void);
int ln_station_current_qa_hold_seconds(void);

int ln_station_export_json(const LnStationContext *ctx, const LnTimingResult *result, char *out_path, unsigned long out_path_len);
int ln_station_submit_result(const LnStationContext *ctx, const LnTimingResult *result, char *response, unsigned long response_len);

#ifdef __cplusplus
}
#endif

#endif
