#include "ln_canonical.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Formats a double exactly the way CPython's float repr does (which is
 * what every value round-trips through via json.loads()+json.dumps()):
 *
 * 1. Find the shortest significant-digit count (1..17) that round-trips
 *    to the same double, via %e (which reports digits independent of
 *    magnitude, unlike %f).
 * 2. Decide fixed vs. scientific notation using CPython's own threshold
 *    for repr -- confirmed directly against Python/pystrtod.c (format_code
 *    'r'): `decpt <= -4 || decpt > 16`, where decpt == exponent+1. In
 *    terms of the base-10 exponent from step 1: scientific notation iff
 *    exponent < -4 or exponent >= 16.
 *
 * An earlier draft tried to dodge this by always using fixed-point
 * (%.*f), which is wrong for any realistically-occurring small value --
 * e.g. a very well-regulated watch's rate could land at 0.00003 s/day,
 * which Python renders as "3e-05". Getting this wrong doesn't error; it
 * just makes every signature on such a value silently verify as
 * "Invalid" on the server, which is a much worse failure mode.
 */
int ln_canonical_format_double(double value, char *buf, size_t buf_len) {
    int precision;
    int sig_figs = 0;
    char sci[64];
    char sign_char = '\0';
    char digits[24];
    int ndigits = 0;
    int exponent = 0;
    int use_exp;
    const char *p;
    const char *e_ptr;
    char local[64];
    int written = 0;

    if (isnan(value) || isinf(value)) {
        /* LN-CANONICAL-JSON-1 rejects NaN/Infinity; a caller should never
         * reach here with such a value. Fail safe rather than emit
         * invalid JSON. */
        return snprintf(buf, buf_len, "0.0");
    }
    if (value == 0.0) {
        /* signbit() distinguishes -0.0 from 0.0; the equality check below
         * (strtod result == value) cannot, since -0.0 == 0.0 in C. */
        return snprintf(buf, buf_len, signbit(value) ? "-0.0" : "0.0");
    }

    for (precision = 0; precision <= 16; precision++) {
        char candidate[64];
        snprintf(candidate, sizeof(candidate), "%.*e", precision, value);
        if (strtod(candidate, NULL) == value) {
            snprintf(sci, sizeof(sci), "%s", candidate);
            sig_figs = precision + 1;
            break;
        }
    }
    if (sig_figs == 0) {
        snprintf(sci, sizeof(sci), "%.16e", value);
        sig_figs = 17;
    }
    (void)sig_figs;

    /* Parse "[-]D[.DDD]e[+-]NN" (guaranteed shape from %e) into sign,
     * significant digits, and base-10 exponent. */
    p = sci;
    if (*p == '-') { sign_char = '-'; p++; }
    digits[ndigits++] = *p++;
    if (*p == '.') {
        p++;
        while (*p && *p != 'e' && *p != 'E' && ndigits < (int)sizeof(digits) - 1) {
            digits[ndigits++] = *p++;
        }
    }
    digits[ndigits] = '\0';
    e_ptr = strchr(sci, 'e');
    if (!e_ptr) e_ptr = strchr(sci, 'E');
    exponent = e_ptr ? atoi(e_ptr + 1) : 0;

    use_exp = (exponent < -4) || (exponent >= 16);

    if (use_exp) {
        written = 0;
        if (sign_char) local[written++] = sign_char;
        local[written++] = digits[0];
        if (ndigits > 1) {
            local[written++] = '.';
            memcpy(local + written, digits + 1, ndigits - 1);
            written += ndigits - 1;
        }
        local[written] = '\0';
        written += snprintf(local + written, sizeof(local) - written, "e%+03d", exponent);
    } else if (exponent >= 0) {
        int int_digits = exponent + 1;
        written = 0;
        if (sign_char) local[written++] = sign_char;
        if (ndigits <= int_digits) {
            memcpy(local + written, digits, ndigits);
            written += ndigits;
            memset(local + written, '0', int_digits - ndigits);
            written += int_digits - ndigits;
            local[written++] = '.';
            local[written++] = '0';
        } else {
            memcpy(local + written, digits, int_digits);
            written += int_digits;
            local[written++] = '.';
            memcpy(local + written, digits + int_digits, ndigits - int_digits);
            written += ndigits - int_digits;
        }
        local[written] = '\0';
    } else {
        int leading_zeros = -exponent - 1;
        written = 0;
        if (sign_char) local[written++] = sign_char;
        local[written++] = '0';
        local[written++] = '.';
        memset(local + written, '0', leading_zeros);
        written += leading_zeros;
        memcpy(local + written, digits, ndigits);
        written += ndigits;
        local[written] = '\0';
    }

    return snprintf(buf, buf_len, "%s", local);
}

/* Matches Python json.dumps()'s default escape table exactly (the two
 * and three-character short escapes it special-cases, plus \u00XX for
 * any other control character). ensure_ascii=False on the server side,
 * so anything >= 0x20 passes through as raw UTF-8. */
static void canonical_escape_string(GString *out, const char *s) {
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    g_string_append_c(out, '"');
    while (*p) {
        switch (*p) {
            case '\\': g_string_append(out, "\\\\"); break;
            case '"':  g_string_append(out, "\\\""); break;
            case '\b': g_string_append(out, "\\b"); break;
            case '\f': g_string_append(out, "\\f"); break;
            case '\n': g_string_append(out, "\\n"); break;
            case '\r': g_string_append(out, "\\r"); break;
            case '\t': g_string_append(out, "\\t"); break;
            default:
                if (*p < 0x20) g_string_append_printf(out, "\\u%04x", *p);
                else g_string_append_c(out, (char)*p);
        }
        p++;
    }
    g_string_append_c(out, '"');
}

static void canonical_sep(GString *out, gboolean *first) {
    if (!*first) g_string_append_c(out, ',');
    *first = FALSE;
}

static void canonical_append_number_field(GString *out, gboolean *first, const char *key, double value) {
    char formatted[64];
    canonical_sep(out, first);
    canonical_escape_string(out, key);
    g_string_append_c(out, ':');
    ln_canonical_format_double(value, formatted, sizeof(formatted));
    g_string_append(out, formatted);
}

static void canonical_append_int_field(GString *out, gboolean *first, const char *key, int value) {
    canonical_sep(out, first);
    canonical_escape_string(out, key);
    g_string_append_c(out, ':');
    g_string_append_printf(out, "%d", value);
}

static void canonical_append_string_field(GString *out, gboolean *first, const char *key, const char *value) {
    canonical_sep(out, first);
    canonical_escape_string(out, key);
    g_string_append_c(out, ':');
    canonical_escape_string(out, value ? value : "");
}

static void canonical_append_bool_field(GString *out, gboolean *first, const char *key, int value) {
    canonical_sep(out, first);
    canonical_escape_string(out, key);
    g_string_append_c(out, ':');
    g_string_append(out, value ? "true" : "false");
}

GString *ln_canonical_timing_result_json(const LnStationContext *ctx, const LnTimingResult *result) {
    GString *out = g_string_sized_new(1024);
    GString *measurements = g_string_sized_new(256);
    GString *baseline = g_string_sized_new(256);
    gboolean first;

    /* "measurements" sub-object -- keys sorted alphabetically, matching
     * Python's sort_keys=True applying recursively to nested dicts. */
    first = TRUE;
    g_string_append_c(measurements, '{');
    canonical_append_number_field(measurements, &first, "amplitude_deg", result->amplitude_deg);
    canonical_append_number_field(measurements, &first, "beat_error_ms", result->beat_error_ms);
    canonical_append_number_field(measurements, &first, "beat_frequency_bph", result->beat_frequency_bph);
    canonical_append_number_field(measurements, &first, "duration_seconds", result->duration_seconds);
    canonical_append_number_field(measurements, &first, "lift_angle_deg", result->lift_angle_deg);
    canonical_append_number_field(measurements, &first, "rate_s_per_day", result->rate_s_per_day);
    canonical_append_number_field(measurements, &first, "sample_rate_hz", result->sample_rate_hz);
    g_string_append_c(measurements, '}');

    /* "baseline" sub-object -- keys sorted alphabetically. */
    first = TRUE;
    g_string_append_c(baseline, '{');
    canonical_append_number_field(baseline, &first, "amplitude_deg", result->baseline_amplitude_deg);
    canonical_append_number_field(baseline, &first, "beat_error_ms", result->baseline_beat_error_ms);
    canonical_append_bool_field(baseline, &first, "has_baseline", result->has_baseline ? 1 : 0);
    canonical_append_number_field(baseline, &first, "rate_s_per_day", result->baseline_rate_s_per_day);
    g_string_append_c(baseline, '}');

    /* Top level -- keys sorted alphabetically. Must exactly match the
     * field set submitted over the wire (minus "signatures"), since the
     * server derives its verification bytes from whatever it parses out
     * of the request body, not from anything the client computed. */
    first = TRUE;
    g_string_append_c(out, '{');

    canonical_sep(out, &first);
    canonical_escape_string(out, "baseline");
    g_string_append_c(out, ':');
    g_string_append(out, baseline->str);

    canonical_append_bool_field(out, &first, "followup_mode", ln_station_is_followup());
    canonical_append_string_field(out, &first, "identity_id", ctx->identity_id);
    canonical_append_string_field(out, &first, "measurement_type", ln_station_current_measurement_type());

    canonical_sep(out, &first);
    canonical_escape_string(out, "measurements");
    g_string_append_c(out, ':');
    g_string_append(out, measurements->str);

    canonical_append_string_field(out, &first, "notes", result->notes);
    canonical_append_string_field(out, &first, "operator_id", ctx->operator_id);
    canonical_append_string_field(out, &first, "position", ctx->position);
    canonical_append_int_field(out, &first, "qa_rate_limit_s_per_day", ln_station_current_qa_rate_limit());
    canonical_append_string_field(out, &first, "qa_standard", ln_station_current_qa_standard());
    canonical_append_string_field(out, &first, "schema", "ln_tg_timing_result_v1");
    canonical_append_string_field(out, &first, "station_id", ctx->station_id);
    canonical_append_string_field(out, &first, "station_version", LN_STATION_VERSION);
    canonical_append_string_field(out, &first, "temperature_condition", ln_station_current_temperature_condition());
    canonical_append_string_field(out, &first, "timestamp_iso", result->timestamp_iso);
    canonical_append_string_field(out, &first, "work_order", ctx->work_order);

    g_string_append_c(out, '}');

    g_string_free(measurements, TRUE);
    g_string_free(baseline, TRUE);
    return out;
}
