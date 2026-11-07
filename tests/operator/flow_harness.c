/* Driven by run_flow_test.py against a fake ERP. Prints one line per step. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ln_station.h"
#include "ln_operator.h"

int main(int argc, char **argv) {
    LnStationContext ctx;
    LnTimingResult result;
    char msg[512], response[4096], path[1024];
    int rc, noted;

    if (argc < 3) return 2;
    setvbuf(stdout, NULL, _IONBF, 0); /* so a crash still shows how far it got */
    ln_station_init(&ctx);
    snprintf(ctx.erp_base_url, sizeof(ctx.erp_base_url), "%s", argv[1]);
    snprintf(ctx.export_dir, sizeof(ctx.export_dir), "%s", argv[2]);
    snprintf(ctx.api_key, sizeof(ctx.api_key), "KEY");
    snprintf(ctx.api_secret, sizeof(ctx.api_secret), "SECRET");
    snprintf(ctx.station_id, sizeof(ctx.station_id), "LN-TEST-01");
    ln_station_set_identity(&ctx, "SER-1");
    ctx.submit_enabled = true;
    ctx.verify_tls = false;
    memset(&result, 0, sizeof(result));
    result.rate_s_per_day = 2.5;
    snprintf(result.timestamp_iso, sizeof(result.timestamp_iso), "2026-11-06T12:00:00Z");

    rc = ln_station_operator_sign_in(&ctx, "OP1", "12345", msg, sizeof(msg));
    printf("short_pin rc=%d msg=%s signed_in=%d\n", rc, msg, ln_station_operator_signed_in(&ctx));

    rc = ln_station_operator_sign_in(&ctx, "OP1", "483921", msg, sizeof(msg));
    printf("wrong_pin rc=%d msg=%s signed_in=%d\n", rc, msg, ln_station_operator_signed_in(&ctx));

    rc = ln_station_operator_sign_in(&ctx, "  OP1 ", "483920", msg, sizeof(msg));
    printf("good_pin rc=%d signed_in=%d operator_id=%s name=%s usable=%d\n", rc,
           ln_station_operator_signed_in(&ctx), ctx.operator_id, ctx.operator_name, ln_station_operator_session_usable(&ctx));

    ctx.operator_last_used -= 20 * 60; /* idle for 20 minutes: still fresh, and a result should refresh it */
    rc = ln_station_submit_result(&ctx, &result, NULL, 0, response, sizeof(response));
    noted = ln_station_operator_note_submit(&ctx, rc, response); /* before the printf: argument order is unspecified */
    printf("submit_a rc=%d noted=%d signed_in=%d touched=%d\n", rc, noted, ln_station_operator_signed_in(&ctx),
           (int)(time(NULL) - ctx.operator_last_used < 5));

    rc = ln_station_export_json(&ctx, &result, NULL, 0, path, sizeof(path));
    printf("export rc=%d path=%s\n", rc, path);

    ln_station_operator_sign_out(&ctx);
    printf("signed_out signed_in=%d\n", ln_station_operator_signed_in(&ctx));

    rc = ln_station_operator_sign_in(&ctx, "OP1", "483920", msg, sizeof(msg));
    printf("second_login rc=%d\n", rc);
    rc = ln_station_submit_result(&ctx, &result, NULL, 0, response, sizeof(response));
    noted = ln_station_operator_note_submit(&ctx, rc, response);
    printf("submit_b rc=%d noted=%d signed_in=%d\n", rc, noted, ln_station_operator_signed_in(&ctx));

    rc = ln_station_submit_result(&ctx, &result, NULL, 0, response, sizeof(response));
    noted = ln_station_operator_note_submit(&ctx, rc, response);
    printf("submit_c rc=%d noted=%d\n", rc, noted);

    /* a sign-in the station's own clock says has gone stale is not sent */
    rc = ln_station_operator_sign_in(&ctx, "OP1", "483920", msg, sizeof(msg));
    ctx.operator_last_used -= 40 * 60;
    printf("stale usable=%d signed_in=%d\n", ln_station_operator_session_usable(&ctx), ln_station_operator_signed_in(&ctx));
    rc = ln_station_submit_result(&ctx, &result, NULL, 0, response, sizeof(response));
    printf("submit_d rc=%d\n", rc);

    /* a failed request (HTTP 500 ...) is not the ERP saying the sign-in was wrong, whatever text came back */
    noted = ln_station_operator_note_submit(&ctx, 500, "Operator sign-in: expired");
    printf("non2xx noted=%d signed_in=%d\n", noted, ln_station_operator_signed_in(&ctx));

    /* nobody signed in beforehand from here on: the panel only offers Sign in then */
    ln_station_operator_forget(&ctx);

    /* an API key the ERP refuses */
    snprintf(ctx.api_key, sizeof(ctx.api_key), "BAD");
    rc = ln_station_operator_sign_in(&ctx, "OP1", "483920", msg, sizeof(msg));
    printf("forbidden rc=%d msg=%s signed_in=%d\n", rc, msg, ln_station_operator_signed_in(&ctx));
    snprintf(ctx.api_key, sizeof(ctx.api_key), "KEY");

    /* an ERP that is down */
    snprintf(ctx.erp_base_url, sizeof(ctx.erp_base_url), "http://127.0.0.1:1");
    rc = ln_station_operator_sign_in(&ctx, "OP1", "483920", msg, sizeof(msg));
    printf("unreachable rc=%d msg=%s signed_in=%d\n", rc, msg, ln_station_operator_signed_in(&ctx));
    return 0;
}
