#include "ln_station.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LN_STATION_VERSION
#define LN_STATION_VERSION "0.9.0"
#endif

static char ln_station_global_position[32] = "Dial Up";
static char ln_station_global_qa_standard[32] = "Workshop";
static char ln_station_global_temperature_condition[32] = "Room / 23C";
static char ln_station_global_measurement_type[48] = "Initial Certification";
static int ln_station_global_followup_mode = 0;
static int ln_station_global_qa_limit = 20;

typedef struct {
    char position[32];
    double rate_s_per_day;
    double beat_error_ms;
    double amplitude_deg;
    int has_value;
} LnStationBaseline;

static LnStationBaseline ln_station_baselines[6];
static time_t ln_station_qa_started_at = 0;
static int ln_station_qa_passed = 0;

static void safe_copy(char *dst, unsigned long dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_len, "%s", src);
}

static void json_escape(FILE *f, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    fputc('"', f);
    while (*p) {
        switch (*p) {
            case '\\': fputs("\\\\", f); break;
            case '"':  fputs("\\\"", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (*p < 32) fprintf(f, "\\u%04x", *p);
                else fputc(*p, f);
        }
        p++;
    }
    fputc('"', f);
}

static void now_compact(char *buf, unsigned long len) {
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(buf, len, "%Y%m%dT%H%M%SZ", &tmv);
}

void ln_station_init(LnStationContext *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    safe_copy(ctx->station_id, sizeof(ctx->station_id), "LN-TG-001");
    safe_copy(ctx->position, sizeof(ctx->position), "Dial Up");
    safe_copy(ctx->qa_standard, sizeof(ctx->qa_standard), "Workshop");
    safe_copy(ctx->temperature_condition, sizeof(ctx->temperature_condition), "Room / 23C");
    safe_copy(ctx->measurement_type, sizeof(ctx->measurement_type), "Initial Certification");
    ctx->followup_mode = false;
    safe_copy(ln_station_global_position, sizeof(ln_station_global_position), ctx->position);
    safe_copy(ctx->export_dir, sizeof(ctx->export_dir), ".");
    ln_station_set_qa_standard("Workshop");
    ln_station_set_temperature_condition("Room / 23C");
    ln_station_set_measurement_type("Initial Certification");
    ctx->export_enabled = true;
    ctx->submit_enabled = false;
}

void ln_station_set_identity(LnStationContext *ctx, const char *identity_id) {
    if (!ctx) return;
    safe_copy(ctx->identity_id, sizeof(ctx->identity_id), identity_id);
}

void ln_station_set_operator(LnStationContext *ctx, const char *operator_id) {
    if (!ctx) return;
    safe_copy(ctx->operator_id, sizeof(ctx->operator_id), operator_id);
}

void ln_station_set_position(LnStationContext *ctx, const char *position) {
    if (!ctx) return;
    safe_copy(ctx->position, sizeof(ctx->position), position);
    safe_copy(ln_station_global_position, sizeof(ln_station_global_position), ctx->position);
    ln_station_qa_started_at = 0;
    ln_station_qa_passed = 0;
}

const char *ln_station_current_position(void) {
    return ln_station_global_position[0] ? ln_station_global_position : "Dial Up";
}

void ln_station_set_qa_standard(const char *standard) {
    if (!standard || !*standard) standard = "Workshop";

    safe_copy(ln_station_global_qa_standard, sizeof(ln_station_global_qa_standard), standard);

    if (strcmp(standard, "Observatory") == 0)
        ln_station_global_qa_limit = 2;
    else if (strcmp(standard, "Signature") == 0)
        ln_station_global_qa_limit = 5;
    else if (strcmp(standard, "Precision") == 0)
        ln_station_global_qa_limit = 10;
    else
        ln_station_global_qa_limit = 20;

    ln_station_qa_started_at = 0;
    ln_station_qa_passed = 0;
}

const char *ln_station_current_qa_standard(void) {
    return ln_station_global_qa_standard[0] ? ln_station_global_qa_standard : "Workshop";
}

void ln_station_set_temperature_condition(const char *temperature_condition) {
    safe_copy(ln_station_global_temperature_condition, sizeof(ln_station_global_temperature_condition),
              temperature_condition && *temperature_condition ? temperature_condition : "Room / 23C");
}

const char *ln_station_current_temperature_condition(void) {
    return ln_station_global_temperature_condition[0] ? ln_station_global_temperature_condition : "Room / 23C";
}

void ln_station_set_measurement_type(const char *measurement_type) {
    safe_copy(ln_station_global_measurement_type, sizeof(ln_station_global_measurement_type),
              measurement_type && *measurement_type ? measurement_type : "Initial Certification");
    ln_station_set_followup_mode(strcmp(ln_station_global_measurement_type, "Follow-up Certification") == 0);
}

const char *ln_station_current_measurement_type(void) {
    return ln_station_global_measurement_type[0] ? ln_station_global_measurement_type : "Initial Certification";
}

void ln_station_set_followup_mode(int followup_mode) {
    ln_station_global_followup_mode = followup_mode ? 1 : 0;
}

int ln_station_is_followup(void) {
    return ln_station_global_followup_mode;
}

void ln_station_set_baseline_for_position(const char *position, double rate_s_per_day, double beat_error_ms, double amplitude_deg) {
    int i;
    if (!position || !*position) return;
    for (i = 0; i < 6; i++) {
        if (!ln_station_baselines[i].has_value || strcmp(ln_station_baselines[i].position, position) == 0) {
            safe_copy(ln_station_baselines[i].position, sizeof(ln_station_baselines[i].position), position);
            ln_station_baselines[i].rate_s_per_day = rate_s_per_day;
            ln_station_baselines[i].beat_error_ms = beat_error_ms;
            ln_station_baselines[i].amplitude_deg = amplitude_deg;
            ln_station_baselines[i].has_value = 1;
            return;
        }
    }
}

int ln_station_get_baseline_for_current_position(double *rate_s_per_day, double *beat_error_ms, double *amplitude_deg) {
    int i;
    const char *position = ln_station_current_position();
    for (i = 0; i < 6; i++) {
        if (ln_station_baselines[i].has_value && strcmp(ln_station_baselines[i].position, position) == 0) {
            if (rate_s_per_day) *rate_s_per_day = ln_station_baselines[i].rate_s_per_day;
            if (beat_error_ms) *beat_error_ms = ln_station_baselines[i].beat_error_ms;
            if (amplitude_deg) *amplitude_deg = ln_station_baselines[i].amplitude_deg;
            return 1;
        }
    }
    return 0;
}

int ln_station_current_qa_rate_limit(void) {
    return ln_station_global_qa_limit;
}

void ln_station_qa_update_rate(double rate_s_per_day, int valid) {
    double abs_rate = rate_s_per_day < 0 ? -rate_s_per_day : rate_s_per_day;
    time_t now_t = time(NULL);

    if (!valid || abs_rate > (double)ln_station_global_qa_limit) {
        ln_station_qa_started_at = 0;
        ln_station_qa_passed = 0;
        return;
    }

    if (!ln_station_qa_started_at)
        ln_station_qa_started_at = now_t;

    if (difftime(now_t, ln_station_qa_started_at) >= 5.0)
        ln_station_qa_passed = 1;
}

int ln_station_position_qa_passed(void) {
    return ln_station_qa_passed;
}

double ln_station_position_qa_seconds(void) {
    if (!ln_station_qa_started_at)
        return 0.0;
    return difftime(time(NULL), ln_station_qa_started_at);
}

int ln_station_export_json(const LnStationContext *ctx, const LnTimingResult *result, char *out_path, unsigned long out_path_len) {
    char stamp[32];
    char safe_identity[160];
    char path[1024];
    FILE *f;
    unsigned long i;

    if (!ctx || !result || !ctx->identity_id[0]) return -1;

    safe_copy(safe_identity, sizeof(safe_identity), ctx->identity_id);
    for (i = 0; safe_identity[i]; i++) {
        if (safe_identity[i] == '/' || safe_identity[i] == '\\' || safe_identity[i] == ':' || safe_identity[i] == ' ')
            safe_identity[i] = '_';
    }

    now_compact(stamp, sizeof(stamp));
    snprintf(path, sizeof(path), "%s/%s_%s.json", ctx->export_dir[0] ? ctx->export_dir : ".", safe_identity, stamp);

    f = fopen(path, "w");
    if (!f) return -2;

    fprintf(f, "{\n");
    fprintf(f, "  \"schema\": \"ln_tg_timing_result_v1\",\n");
    fprintf(f, "  \"station_version\": "); json_escape(f, LN_STATION_VERSION); fprintf(f, ",\n");
    fprintf(f, "  \"station_id\": "); json_escape(f, ctx->station_id); fprintf(f, ",\n");
    fprintf(f, "  \"operator_id\": "); json_escape(f, ctx->operator_id); fprintf(f, ",\n");
    fprintf(f, "  \"identity_id\": "); json_escape(f, ctx->identity_id); fprintf(f, ",\n");
    fprintf(f, "  \"work_order\": "); json_escape(f, ctx->work_order); fprintf(f, ",\n");
    fprintf(f, "  \"position\": "); json_escape(f, ctx->position); fprintf(f, ",\n");
    fprintf(f, "  \"qa_standard\": "); json_escape(f, ln_station_current_qa_standard()); fprintf(f, ",\n");
    fprintf(f, "  \"qa_rate_limit_s_per_day\": %d,\n", ln_station_current_qa_rate_limit());
    fprintf(f, "  \"temperature_condition\": "); json_escape(f, ln_station_current_temperature_condition()); fprintf(f, ",\n");
    fprintf(f, "  \"measurement_type\": "); json_escape(f, ln_station_current_measurement_type()); fprintf(f, ",\n");
    fprintf(f, "  \"followup_mode\": %s,\n", ln_station_is_followup() ? "true" : "false");
    fprintf(f, "  \"timestamp_iso\": "); json_escape(f, result->timestamp_iso); fprintf(f, ",\n");
    fprintf(f, "  \"measurements\": {\n");
    fprintf(f, "    \"rate_s_per_day\": %.6f,\n", result->rate_s_per_day);
    fprintf(f, "    \"beat_error_ms\": %.6f,\n", result->beat_error_ms);
    fprintf(f, "    \"amplitude_deg\": %.6f,\n", result->amplitude_deg);
    fprintf(f, "    \"beat_frequency_bph\": %.6f,\n", result->beat_frequency_bph);
    fprintf(f, "    \"lift_angle_deg\": %.6f,\n", result->lift_angle_deg);
    fprintf(f, "    \"sample_rate_hz\": %.6f,\n", result->sample_rate_hz);
    fprintf(f, "    \"duration_seconds\": %.6f\n", result->duration_seconds);
    fprintf(f, "  },\n");
    fprintf(f, "  \"baseline\": {\n");
    fprintf(f, "    \"has_baseline\": %s,\n", result->has_baseline ? "true" : "false");
    fprintf(f, "    \"rate_s_per_day\": %.6f,\n", result->baseline_rate_s_per_day);
    fprintf(f, "    \"beat_error_ms\": %.6f,\n", result->baseline_beat_error_ms);
    fprintf(f, "    \"amplitude_deg\": %.6f\n", result->baseline_amplitude_deg);
    fprintf(f, "  },\n");
    fprintf(f, "  \"notes\": "); json_escape(f, result->notes); fprintf(f, "\n");
    fprintf(f, "}\n");
    fclose(f);

    if (out_path && out_path_len > 0) safe_copy(out_path, out_path_len, path);
    return 0;
}

int ln_station_submit_result(const LnStationContext *ctx, const LnTimingResult *result, char *response, unsigned long response_len) {
    (void)result;
    if (!ctx) return -1;
    if (!ctx->submit_enabled) {
        safe_copy(response, response_len, "submit disabled; JSON export only");
        return 1;
    }
    safe_copy(response, response_len, "submit not implemented in v0.2.1");
    return 2;
}
