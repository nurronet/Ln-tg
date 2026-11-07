#ifndef LN_OPERATOR_H
#define LN_OPERATOR_H

/* Operator PIN sign-in: the parts that need no GTK, curl or ERP connection,
 * so tests/operator can check them on their own.
 *
 * The protocol is written up in the ERP repo at
 * docs/architecture/operator_pin_and_attestation.md. In short: the operator
 * types an Operator ID and PIN, the station POSTs them to
 * ln_watch_inventory.freeerp.operator_login, and the ERP answers with a
 * session token. The station keeps the token in memory (never on disk, in a
 * log or in an export) and sends it with each result as "operator_session",
 * outside the data it signs. The PIN is forgotten as soon as the call returns.
 */

#include <stddef.h>
#include <time.h>

/* The ERP ends a sign-in 8 hours after it began, or after 30 minutes
 * without a result. The station stops using a token a little before that
 * so a stale one is never sent: a result sent with an ended sign-in arrives
 * at the ERP as Invalid, and by then the position results are already gone
 * from the station. */
#define LN_OPERATOR_MAX_SECONDS (7 * 3600 + 50 * 60)
#define LN_OPERATOR_IDLE_SECONDS (25 * 60)

typedef struct {
    int ok;                  /* 1 = signed in, 0 = the ERP refused */
    char reason[256];        /* when refused: the ERP's own wording, shown as is */
    char operator_id[64];    /* when signed in */
    char operator_name[96];
    char session[96];        /* the token; treat as a secret */
} LnOperatorLogin;

/* Reads the ERP's answer to operator_login:
 *   {"message": {"ok": true, "operator_id": ..., "operator_name": ..., "session": ...}}
 *   {"message": {"ok": false, "reason": ...}}
 * Returns 0 when it was understood (see out->ok), -1 when it was not (not
 * JSON, no "message" object, no "ok", or "ok" true without a session). */
int ln_operator_parse_login(const char *json, LnOperatorLogin *out);

/* 6 to 8 ASCII digits. Checked before sending so a mistyped PIN never
 * costs the operator one of their five tries. Weak PINs (repeated digits,
 * simple runs) cannot exist on the ERP side, so a pattern here is simply
 * a wrong PIN; only the length and the characters are checked. */
int ln_operator_pin_format_ok(const char *pin);

/* Does this ERP response to submit_timing_result say the operator sign-in
 * was not accepted? (The ERP's message then starts "Operator sign-in:".) */
int ln_operator_response_rejected_signin(const char *response);

/* Is a sign-in made at `signed_at` and last used at `last_used` still safe
 * to send at `now`? */
int ln_operator_session_fresh(time_t signed_at, time_t last_used, time_t now);

/* Overwrites memory in a way the compiler may not remove (a PIN buffer,
 * a response holding a token). */
void ln_operator_wipe(void *p, size_t n);

#endif
