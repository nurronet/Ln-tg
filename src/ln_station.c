#include "ln_station.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LN_STATION_VERSION
#define LN_STATION_VERSION "0.2.1"
#endif

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
    safe_copy(ctx->export_dir, sizeof(ctx->export_dir), ".");
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
