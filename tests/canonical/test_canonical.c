/* See README.md in this directory for how to build/run this and why it
 * exists. Prints one canonical-JSON line per test case; compare against
 * reference.py's output for the same cases. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "ln_station.h"
#include "ln_canonical.h"

static void run_case(const char *identity_id, const char *station_id, const char *operator_id,
                      const char *work_order, const char *position, const char *qa_standard,
                      const char *temperature_condition, const char *measurement_type,
                      int followup_mode, const LnTimingResult *result) {
    LnStationContext ctx;
    GString *out;

    ln_station_init(&ctx);
    ln_station_set_identity(&ctx, identity_id);
    snprintf(ctx.station_id, sizeof(ctx.station_id), "%s", station_id);
    snprintf(ctx.operator_id, sizeof(ctx.operator_id), "%s", operator_id);
    snprintf(ctx.work_order, sizeof(ctx.work_order), "%s", work_order);
    ln_station_set_position(&ctx, position);
    ln_station_set_qa_standard(qa_standard);
    ln_station_set_temperature_condition(temperature_condition);
    ln_station_set_measurement_type(measurement_type);
    ln_station_set_followup_mode(followup_mode);

    out = ln_canonical_timing_result_json(&ctx, result);
    printf("%s\n", out->str);
    g_string_free(out, TRUE);
}

int main(void) {
    LnTimingResult r;

    /* Case 1: ordinary values. */
    memset(&r, 0, sizeof(r));
    r.rate_s_per_day = 2.5;
    r.beat_error_ms = 0.3;
    r.amplitude_deg = 245.5;
    r.beat_frequency_bph = 28800.0;
    r.lift_angle_deg = 52.0;
    r.sample_rate_hz = 44100.0;
    r.duration_seconds = 20.0;
    r.has_baseline = true;
    r.baseline_rate_s_per_day = -1.2;
    r.baseline_beat_error_ms = 0.1;
    r.baseline_amplitude_deg = 250.0;
    snprintf(r.timestamp_iso, sizeof(r.timestamp_iso), "2026-07-21T12:00:00Z");
    snprintf(r.notes, sizeof(r.notes), "Test \"quoted\" and \\backslash\\ and line\nbreak");
    run_case("MOVEMENT-2892-00042", "LN-TG-001", "nick", "LN-MFG-00007",
              "Dial Up", "Precision", "Room / 23C", "Initial Certification", 0, &r);

    /* Case 2: float precision edge cases -- negative zero, 0.1, the
     * classic 0.1+0.2 artifact, and a "round" whole number (20.0, the
     * one that broke an earlier %g-based draft). */
    memset(&r, 0, sizeof(r));
    r.rate_s_per_day = 0.1;
    r.beat_error_ms = -0.0;
    r.amplitude_deg = 1000.0;
    r.beat_frequency_bph = 0.0;
    r.lift_angle_deg = -52.75;
    r.sample_rate_hz = 96000.0;
    r.duration_seconds = 0.30000000000000004;
    r.has_baseline = false;
    r.baseline_rate_s_per_day = 100000.0;
    r.baseline_beat_error_ms = 0.0001;
    r.baseline_amplitude_deg = -0.5;
    snprintf(r.timestamp_iso, sizeof(r.timestamp_iso), "2026-01-01T00:00:00Z");
    snprintf(r.notes, sizeof(r.notes), "%s", "");
    run_case("X", "LN-TG-001", "", "", "Crown Up", "Observatory", "Room / 23C",
              "Follow-up Certification", 1, &r);

    /* Case 3: UTF-8 passthrough and the scientific-notation boundary
     * (0.000001 -> Python repr "1e-06"). */
    memset(&r, 0, sizeof(r));
    r.rate_s_per_day = 999999.0;
    r.beat_error_ms = 0.000001;
    r.amplitude_deg = 359.999999;
    r.beat_frequency_bph = 21600.0;
    r.lift_angle_deg = 0.0;
    r.sample_rate_hz = 22050.0;
    r.duration_seconds = 1.0;
    r.has_baseline = true;
    r.baseline_rate_s_per_day = 0.0;
    r.baseline_beat_error_ms = 0.0;
    r.baseline_amplitude_deg = 0.0;
    snprintf(r.timestamp_iso, sizeof(r.timestamp_iso), "2026-01-01T00:00:00Z");
    snprintf(r.notes, sizeof(r.notes), "na\xc3\xafve caf\xc3\xa9 \xe2\x9c\x93");
    run_case("cafe-01", "LN-TG-001", "", "", "Dial Down", "Signature", "21\xc2\xb0""C",
              "Service Check", 0, &r);

    return 0;
}
