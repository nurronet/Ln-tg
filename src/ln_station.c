#include "ln_station.h"

#include <curl/curl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ln_canonical.h"
#include "ln_signing.h"

static char ln_station_global_position[32] = "Dial Up";
static char ln_station_global_qa_standard[32] = "Workshop";
static char ln_station_global_temperature_condition[32] = "Room / 23C";
static char ln_station_global_measurement_type[48] = "Initial Certification";
static int ln_station_global_followup_mode = 0;
static int ln_station_global_qa_limit = 20;
static int ln_station_global_qa_hold_seconds = 8;

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

/* Sample-capture window state -- see LnCaptureStatus in ln_station.h.
 * 400 samples covers the longest hold (Observatory, 20s) several times
 * over even at a fast tick rate; capture simply stops appending past
 * that rather than growing unbounded. */
#define LN_STATION_MAX_CAPTURE_SAMPLES 400
typedef struct {
    double rate_s_per_day;
    double beat_error_ms;
    double amplitude_deg;
    double bph;
} LnCaptureSample;
static LnCaptureSample ln_station_capture_samples[LN_STATION_MAX_CAPTURE_SAMPLES];
static int ln_station_capture_sample_count = 0;
static int ln_station_capture_active = 0;
static int ln_station_capture_auto_mode = 0;
static time_t ln_station_capture_started_at = 0;

static void ln_station_capture_reset_for_new_position(void);

static void safe_copy(char *dst, unsigned long dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_len, "%s", src);
}

static void json_escape_gstring(GString *out, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    g_string_append_c(out, '"');
    while (*p) {
        switch (*p) {
            case '\\': g_string_append(out, "\\\\"); break;
            case '"':  g_string_append(out, "\\\""); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            default:
                if (*p < 32) g_string_append_printf(out, "\\u%04x", *p);
                else g_string_append_c(out, (char)*p);
        }
        p++;
    }
    g_string_append_c(out, '"');
}

/* Shared by ln_station_export_json (local file) and ln_station_submit_result
 * (HTTP POST body) so both always serialize the ln_tg_timing_result_v1
 * schema identically. Caller owns the returned GString.
 *
 * `signatures`/`signature_count` are optional (pass NULL/0 for an unsigned
 * export or submission -- the field is omitted entirely rather than sent
 * as an empty array, matching the server's own "no signatures key present"
 * check in _verify_measurement_signatures). This is the human-readable
 * wire/export form; it is NOT the canonical form used for signing -- see
 * ln_canonical_timing_result_json() for that. */
static GString *build_timing_result_json(const LnStationContext *ctx, const LnTimingResult *result,
                                          const LnMeasurementSignature *signatures, int signature_count) {
    GString *out = g_string_sized_new(1024);

    g_string_append(out, "{\n");
    g_string_append(out, "  \"schema\": \"ln_tg_timing_result_v1\",\n");
    g_string_append(out, "  \"station_version\": "); json_escape_gstring(out, LN_STATION_VERSION); g_string_append(out, ",\n");
    g_string_append(out, "  \"station_id\": "); json_escape_gstring(out, ctx->station_id); g_string_append(out, ",\n");
    g_string_append(out, "  \"operator_id\": "); json_escape_gstring(out, ctx->operator_id); g_string_append(out, ",\n");
    g_string_append(out, "  \"identity_id\": "); json_escape_gstring(out, ctx->identity_id); g_string_append(out, ",\n");
    g_string_append(out, "  \"work_order\": "); json_escape_gstring(out, ctx->work_order); g_string_append(out, ",\n");
    g_string_append(out, "  \"position\": "); json_escape_gstring(out, ctx->position); g_string_append(out, ",\n");
    g_string_append(out, "  \"qa_standard\": "); json_escape_gstring(out, ln_station_current_qa_standard()); g_string_append(out, ",\n");
    g_string_append_printf(out, "  \"qa_rate_limit_s_per_day\": %d,\n", ln_station_current_qa_rate_limit());
    g_string_append(out, "  \"temperature_condition\": "); json_escape_gstring(out, ln_station_current_temperature_condition()); g_string_append(out, ",\n");
    g_string_append(out, "  \"measurement_type\": "); json_escape_gstring(out, ln_station_current_measurement_type()); g_string_append(out, ",\n");
    g_string_append_printf(out, "  \"followup_mode\": %s,\n", ln_station_is_followup() ? "true" : "false");
    g_string_append(out, "  \"timestamp_iso\": "); json_escape_gstring(out, result->timestamp_iso); g_string_append(out, ",\n");
    g_string_append(out, "  \"measurements\": {\n");
    g_string_append_printf(out, "    \"rate_s_per_day\": %.6f,\n", result->rate_s_per_day);
    g_string_append_printf(out, "    \"beat_error_ms\": %.6f,\n", result->beat_error_ms);
    g_string_append_printf(out, "    \"amplitude_deg\": %.6f,\n", result->amplitude_deg);
    g_string_append_printf(out, "    \"beat_frequency_bph\": %.6f,\n", result->beat_frequency_bph);
    g_string_append_printf(out, "    \"lift_angle_deg\": %.6f,\n", result->lift_angle_deg);
    g_string_append_printf(out, "    \"sample_rate_hz\": %.6f,\n", result->sample_rate_hz);
    g_string_append_printf(out, "    \"duration_seconds\": %.6f\n", result->duration_seconds);
    g_string_append(out, "  },\n");
    g_string_append(out, "  \"baseline\": {\n");
    g_string_append_printf(out, "    \"has_baseline\": %s,\n", result->has_baseline ? "true" : "false");
    g_string_append_printf(out, "    \"rate_s_per_day\": %.6f,\n", result->baseline_rate_s_per_day);
    g_string_append_printf(out, "    \"beat_error_ms\": %.6f,\n", result->baseline_beat_error_ms);
    g_string_append_printf(out, "    \"amplitude_deg\": %.6f\n", result->baseline_amplitude_deg);
    g_string_append(out, "  },\n");
    if (signatures && signature_count > 0) {
        int i;
        g_string_append(out, "  \"notes\": "); json_escape_gstring(out, result->notes); g_string_append(out, ",\n");
        g_string_append(out, "  \"signatures\": [\n");
        for (i = 0; i < signature_count; i++) {
            g_string_append(out, "    {\"key_id\": ");
            json_escape_gstring(out, signatures[i].key_id);
            g_string_append(out, ", \"signer_type\": ");
            json_escape_gstring(out, signatures[i].signer_type);
            g_string_append(out, ", \"signature_b64\": ");
            json_escape_gstring(out, signatures[i].signature_b64);
            g_string_append(out, i + 1 < signature_count ? "},\n" : "}\n");
        }
        g_string_append(out, "  ]\n");
    } else {
        g_string_append(out, "  \"notes\": "); json_escape_gstring(out, result->notes); g_string_append(out, "\n");
    }
    g_string_append(out, "}\n");

    return out;
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
    ctx->verify_tls = true;
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
    ln_station_capture_reset_for_new_position();
}

const char *ln_station_current_position(void) {
    return ln_station_global_position[0] ? ln_station_global_position : "Dial Up";
}

void ln_station_set_qa_standard(const char *standard) {
    if (!standard || !*standard) standard = "Workshop";

    safe_copy(ln_station_global_qa_standard, sizeof(ln_station_global_qa_standard), standard);

    /* Hold duration scales with strictness, same direction as the rate
     * tolerance tightening: a tier demanding +-2 s/day should also demand
     * a longer stable hold than one accepting +-20 s/day. */
    if (strcmp(standard, "Observatory") == 0) {
        ln_station_global_qa_limit = 2;
        ln_station_global_qa_hold_seconds = 20;
    } else if (strcmp(standard, "Signature") == 0) {
        ln_station_global_qa_limit = 5;
        ln_station_global_qa_hold_seconds = 15;
    } else if (strcmp(standard, "Precision") == 0) {
        ln_station_global_qa_limit = 10;
        ln_station_global_qa_hold_seconds = 10;
    } else {
        ln_station_global_qa_limit = 20;
        ln_station_global_qa_hold_seconds = 8;
    }

    ln_station_qa_started_at = 0;
    ln_station_qa_passed = 0;
}

const char *ln_station_current_qa_standard(void) {
    return ln_station_global_qa_standard[0] ? ln_station_global_qa_standard : "Workshop";
}

int ln_station_current_qa_hold_seconds(void) {
    return ln_station_global_qa_hold_seconds;
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

/* Called from ln_station_set_position on every position change. Always
 * clears the window (a capture from the previous position must never
 * bleed into the next one's average) and, once auto mode has latched
 * (i.e. Start Capture has been pressed at least once this session),
 * immediately re-arms capture for the new position with no button press
 * required. */
static void ln_station_capture_reset_for_new_position(void) {
    ln_station_capture_sample_count = 0;
    ln_station_capture_started_at = 0;
    ln_station_capture_active = ln_station_capture_auto_mode;
}

void ln_station_capture_begin(void) {
    ln_station_capture_sample_count = 0;
    ln_station_capture_started_at = 0;
    ln_station_capture_active = 1;
    ln_station_capture_auto_mode = 1;
}

void ln_station_capture_feed(double rate_s_per_day, double beat_error_ms, double amplitude_deg, double bph, int valid) {
    time_t now_t;

    if (!ln_station_capture_active) return;

    if (!valid) {
        /* Dropped/invalid reading mid-hold -- the window must restart
         * from a genuinely continuous stable read, same principle as the
         * always-on rate hold indicator. */
        ln_station_capture_sample_count = 0;
        ln_station_capture_started_at = 0;
        return;
    }

    now_t = time(NULL);
    if (!ln_station_capture_started_at)
        ln_station_capture_started_at = now_t;

    if (ln_station_capture_sample_count < LN_STATION_MAX_CAPTURE_SAMPLES) {
        LnCaptureSample *s = &ln_station_capture_samples[ln_station_capture_sample_count];
        s->rate_s_per_day = rate_s_per_day;
        s->beat_error_ms = beat_error_ms;
        s->amplitude_deg = amplitude_deg;
        s->bph = bph;
        ln_station_capture_sample_count++;
    }
}

void ln_station_capture_status(LnCaptureStatus *out) {
    int i;
    double sum_rate = 0.0, sum_be = 0.0, sum_amp = 0.0, sum_bph = 0.0;

    if (!out) return;
    memset(out, 0, sizeof(*out));

    out->active = ln_station_capture_active;
    out->required_seconds = ln_station_global_qa_hold_seconds;
    out->sample_count = ln_station_capture_sample_count;
    out->elapsed_seconds = ln_station_capture_started_at ? difftime(time(NULL), ln_station_capture_started_at) : 0.0;
    out->complete = ln_station_capture_active && ln_station_capture_sample_count > 0
                    && out->elapsed_seconds >= (double)ln_station_global_qa_hold_seconds;

    for (i = 0; i < ln_station_capture_sample_count; i++) {
        sum_rate += ln_station_capture_samples[i].rate_s_per_day;
        sum_be += ln_station_capture_samples[i].beat_error_ms;
        sum_amp += ln_station_capture_samples[i].amplitude_deg;
        sum_bph += ln_station_capture_samples[i].bph;
    }
    if (ln_station_capture_sample_count > 0) {
        out->avg_rate_s_per_day = sum_rate / ln_station_capture_sample_count;
        out->avg_beat_error_ms = sum_be / ln_station_capture_sample_count;
        out->avg_amplitude_deg = sum_amp / ln_station_capture_sample_count;
        out->avg_bph = sum_bph / ln_station_capture_sample_count;
    }
}

int ln_station_capture_is_auto(void) {
    return ln_station_capture_auto_mode;
}

int ln_station_export_json(const LnStationContext *ctx, const LnTimingResult *result, char *out_path, unsigned long out_path_len) {
    char stamp[32];
    char safe_identity[160];
    char path[1024];
    GString *body;
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

    body = build_timing_result_json(ctx, result, NULL, 0);
    fputs(body->str, f);
    g_string_free(body, TRUE);
    fclose(f);

    if (out_path && out_path_len > 0) safe_copy(out_path, out_path_len, path);
    return 0;
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} LnResponseBuffer;

static size_t ln_response_write(void *contents, size_t size, size_t nmemb, void *user_data) {
    size_t bytes = size * nmemb;
    LnResponseBuffer *response = (LnResponseBuffer *)user_data;
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

/* Shared by ln_station_submit_result and ln_station_register_workstation_key
 * -- both are "POST a JSON body to an ERP method, read the response" calls
 * that otherwise duplicate the same ~20 lines of curl boilerplate. */
static int ln_station_http_post_json(const LnStationContext *ctx, const char *method_path,
                                      const char *json_body, char *response, unsigned long response_len) {
    CURL *curl;
    CURLcode curl_result;
    struct curl_slist *headers = NULL;
    char url[768];
    char auth[640];
    char user_agent[96];
    long status = 0;
    LnResponseBuffer resp_buf;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        safe_copy(response, response_len, "Unable to initialize HTTP client");
        return -2;
    }

    g_snprintf(url, sizeof(url), "%s/api/method/%s", ctx->erp_base_url, method_path);
    g_snprintf(auth, sizeof(auth), "Authorization: token %s:%s", ctx->api_key, ctx->api_secret);
    g_snprintf(user_agent, sizeof(user_agent), "User-Agent: LN-Watchmaker-Station/%s", LN_STATION_VERSION);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, user_agent);

    resp_buf.buffer = response;
    resp_buf.capacity = response_len;
    resp_buf.length = 0;
    if (response && response_len) response[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json_body));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ln_response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, ctx->verify_tls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, ctx->verify_tls ? 2L : 0L);

    curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (curl_result != CURLE_OK) {
        safe_copy(response, response_len, curl_easy_strerror(curl_result));
        return curl_result == CURLE_OPERATION_TIMEDOUT ? -408 : -3;
    }
    if (status < 200 || status >= 300) {
        return (int)status;
    }
    return 0;
}

static LnSigningKeypair g_workstation_keypair;
static int g_workstation_keypair_ready = 0;

static const char *ln_station_workstation_key_path(void) {
    static char path[1024];
    char dir[900];
    g_snprintf(dir, sizeof(dir), "%s%c%s", g_get_user_config_dir(), G_DIR_SEPARATOR, "ln-watchmaker-station");
    g_mkdir_with_parents(dir, 0700);
    g_snprintf(path, sizeof(path), "%s%c%s", dir, G_DIR_SEPARATOR, "workstation_signing_key.bin");
    return path;
}

/* Tells the ERP about a newly-generated workstation key so custody.py's
 * verify_signature() (and _verify_measurement_signatures(), which reuses
 * it) can recognize it. Not fatal if this fails -- the key still signs
 * fine locally, it just won't verify as "Verified" server-side until
 * registration succeeds (e.g. custody.register_key can be called again
 * later, or an admin registers it manually). Known limitation: if the
 * local key file is ever lost, a freshly generated replacement will reuse
 * the same deterministic key_id but a different public key, and the ERP
 * will reject the second registration ("Key ID already exists") --
 * recovering from that needs an admin to update/revoke the old
 * LN Signing Key record. Out of scope for this workstation-only first
 * pass; not solved here. */
static int ln_station_register_workstation_key(const LnStationContext *ctx, const LnSigningKeypair *keypair,
                                                char *error, size_t error_len) {
    char public_key_b64[128];
    char body[512];
    char response[2048];
    int rc;

    if (ln_signing_public_key_base64(keypair, public_key_b64, sizeof(public_key_b64)) != 0) {
        safe_copy(error, error_len, "Unable to encode workstation public key");
        return -1;
    }

    g_snprintf(body, sizeof(body),
               "{\"key_id\":\"%s\",\"owner_type\":\"Workstation\",\"public_key_b64\":\"%s\"}",
               keypair->key_id, public_key_b64);

    rc = ln_station_http_post_json(ctx, "ln_watch_inventory.custody.register_key", body, response, sizeof(response));
    if (rc != 0) {
        safe_copy(error, error_len, response[0] ? response : "Unknown error registering workstation key");
        return rc;
    }
    return 0;
}

/* Loads the station's persistent signing key, generating and registering
 * one on first use. Cached for the process lifetime -- safe to call before
 * every submission. Failure here is never fatal to the submission itself;
 * callers fall back to submitting unsigned (matches today's behavior)
 * rather than blocking the whole measurement pipeline over a signing
 * hiccup, consistent with how a failed ERP submit already falls back to
 * "JSON export only" elsewhere in this file. */
static int ln_station_ensure_workstation_key(const LnStationContext *ctx, char *error, size_t error_len) {
    const char *path;
    char key_id[96];

    if (g_workstation_keypair_ready) return 0;

    if (ln_signing_init() != 0) {
        safe_copy(error, error_len, "Unable to initialize signing library");
        return -1;
    }

    g_snprintf(key_id, sizeof(key_id), "%s-workstation", ctx->station_id[0] ? ctx->station_id : "LN-TG-001");
    path = ln_station_workstation_key_path();

    if (ln_signing_load_private_key(&g_workstation_keypair, path, key_id) == 0) {
        g_workstation_keypair_ready = 1;
        return 0;
    }

    if (ln_signing_generate(&g_workstation_keypair, key_id) != 0) {
        safe_copy(error, error_len, "Unable to generate workstation signing key");
        return -1;
    }
    if (ln_signing_save_private_key(&g_workstation_keypair, path) != 0) {
        safe_copy(error, error_len, "Unable to save workstation signing key");
        return -1;
    }

    {
        char register_error[512] = "";
        if (ln_station_register_workstation_key(ctx, &g_workstation_keypair, register_error, sizeof(register_error)) != 0) {
            fprintf(stderr, "[LNWS SIGN] WARNING: workstation key registration failed, submissions will sign but may not verify until this succeeds: %s\n",
                    register_error);
            fflush(stderr);
        } else {
            fprintf(stderr, "[LNWS SIGN] Registered new workstation signing key '%s'\n", key_id);
            fflush(stderr);
        }
    }

    g_workstation_keypair_ready = 1;
    return 0;
}

int ln_station_submit_result(const LnStationContext *ctx, const LnTimingResult *result, char *response, unsigned long response_len) {
    CURL *curl;
    CURLcode curl_result;
    struct curl_slist *headers = NULL;
    GString *body;
    char url[768];
    char auth[640];
    char user_agent[96];
    long status = 0;
    double total_seconds = 0.0;
    double connect_seconds = 0.0;
    LnResponseBuffer resp_buf;
    LnMeasurementSignature signature;
    int signature_count = 0;

    if (!ctx || !result) return -1;
    if (!ctx->submit_enabled) {
        safe_copy(response, response_len, "submit disabled; JSON export only");
        return 1;
    }
    if (!ctx->erp_base_url[0] || !ctx->api_key[0] || !ctx->api_secret[0]) {
        safe_copy(response, response_len, "ERP connection is not fully configured (base URL, API key, and secret are required)");
        return -1;
    }
    if (!ctx->identity_id[0]) {
        safe_copy(response, response_len, "An identity is required before submitting a result");
        return -1;
    }

    /* Sign with the workstation's own key (operator signing is a separate,
     * later piece -- this submission will carry only one of the two
     * signatures custody.py's verification wants, so the server-side
     * verification_status will show "Invalid" rather than "Verified"
     * until operator credentials exist. That's an accurate, expected
     * interim state, not a bug: see docs/architecture-debt-map.md's
     * custody ledger notes. A signing failure here is not fatal to the
     * submission -- it just falls back to submitting unsigned. */
    memset(&signature, 0, sizeof(signature));
    {
        char sign_error[256] = "";

        if (ln_station_ensure_workstation_key(ctx, sign_error, sizeof(sign_error)) == 0 && g_workstation_keypair_ready) {
            GString *canonical = ln_canonical_timing_result_json(ctx, result);
            if (ln_signing_sign_base64(&g_workstation_keypair, (const unsigned char *)canonical->str, canonical->len,
                                        signature.signature_b64, sizeof(signature.signature_b64)) == 0) {
                safe_copy(signature.key_id, sizeof(signature.key_id), g_workstation_keypair.key_id);
                safe_copy(signature.signer_type, sizeof(signature.signer_type), "Workstation");
                signature_count = 1;
            } else {
                fprintf(stderr, "[LNWS SIGN] WARNING: failed to sign timing result, submitting unsigned\n");
                fflush(stderr);
            }
            g_string_free(canonical, TRUE);
        } else {
            fprintf(stderr, "[LNWS SIGN] WARNING: workstation key unavailable (%s), submitting unsigned\n",
                    sign_error[0] ? sign_error : "unknown error");
            fflush(stderr);
        }
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        safe_copy(response, response_len, "Unable to initialize HTTP client");
        return -2;
    }

    body = build_timing_result_json(ctx, result, signature_count > 0 ? &signature : NULL, signature_count);

    g_snprintf(url, sizeof(url), "%s/api/method/ln_watch_inventory.freeerp.submit_timing_result", ctx->erp_base_url);
    g_snprintf(auth, sizeof(auth), "Authorization: token %s:%s", ctx->api_key, ctx->api_secret);
    g_snprintf(user_agent, sizeof(user_agent), "User-Agent: LN-Watchmaker-Station/%s", LN_STATION_VERSION);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, user_agent);

    resp_buf.buffer = response;
    resp_buf.capacity = response_len;
    resp_buf.length = 0;
    if (response && response_len) response[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->str);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body->len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ln_response_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, ctx->verify_tls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, ctx->verify_tls ? 2L : 0L);

    fprintf(stderr, "[LNWS SUBMIT] POST %s (identity=%s position=%s, %s)\n", url, ctx->identity_id, ctx->position,
            signature_count > 0 ? "signed" : "unsigned");
    fflush(stderr);

    curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_seconds);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_seconds);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    g_string_free(body, TRUE);

    if (curl_result != CURLE_OK) {
        char message[1024];
        g_snprintf(message, sizeof(message),
                   "Network error: %s (connect timeout 8s, total timeout 20s)",
                   curl_easy_strerror(curl_result));
        fprintf(stderr, "[LNWS SUBMIT] ERROR: %s\n", message);
        fflush(stderr);
        safe_copy(response, response_len, message);
        return curl_result == CURLE_OPERATION_TIMEDOUT ? -408 : -3;
    }

    if (status < 200 || status >= 300) {
        char body_excerpt[1200];
        char message[1800];
        safe_copy(body_excerpt, sizeof(body_excerpt), response && response[0] ? response : "No response body");
        g_snprintf(message, sizeof(message),
                   "HTTP %ld after %.0f ms (connect %.0f ms)\n%s",
                   status, total_seconds * 1000.0, connect_seconds * 1000.0, body_excerpt);
        fprintf(stderr, "[LNWS SUBMIT] HTTP ERROR: %s\n", message);
        fflush(stderr);
        safe_copy(response, response_len, message);
        return (int)status;
    }

    fprintf(stderr, "[LNWS SUBMIT] SUCCESS: HTTP %ld in %.0f ms (connect %.0f ms)\n",
            status, total_seconds * 1000.0, connect_seconds * 1000.0);
    fflush(stderr);
    return 0;
}
