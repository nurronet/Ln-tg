/* See README.md in this directory. Plain asserts; prints "ok" or the first failure. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ln_operator.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void test_login_ok(void) {
    LnOperatorLogin r;
    const char *j = "{\"message\": {\"ok\": true, \"operator_id\": \"OP1\", \"operator_name\": \"Ana Ruiz\", "
                    "\"session\": \"abc_DEF-123\", \"expires_at\": \"2026-11-07 06:00:00\"}}";
    CHECK(ln_operator_parse_login(j, &r) == 0);
    CHECK(r.ok == 1);
    CHECK(strcmp(r.operator_id, "OP1") == 0);
    CHECK(strcmp(r.operator_name, "Ana Ruiz") == 0);
    CHECK(strcmp(r.session, "abc_DEF-123") == 0);
    /* compact form, members in another order, other members around */
    j = "{\"message\":{\"expires_at\":\"x\",\"session\":\"tok\",\"ok\":true,\"operator_id\":\"OP2\"},\"home_page\":\"/app\"}";
    CHECK(ln_operator_parse_login(j, &r) == 0 && r.ok == 1 && strcmp(r.session, "tok") == 0 && strcmp(r.operator_id, "OP2") == 0);
}

static void test_login_refused(void) {
    LnOperatorLogin r;
    CHECK(ln_operator_parse_login("{\"message\": {\"ok\": false, \"reason\": \"The Operator ID or PIN is wrong.\"}}", &r) == 0);
    CHECK(r.ok == 0 && strcmp(r.reason, "The Operator ID or PIN is wrong.") == 0 && r.session[0] == '\0');
    /* escapes and \u in the reason */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":false,\"reason\":\"Locked until 14:05 \\u2014 ask \\\"admin\\\".\"}}", &r) == 0);
    CHECK(strcmp(r.reason, "Locked until 14:05 \xE2\x80\x94 ask \"admin\".") == 0);
    /* a lone surrogate becomes U+FFFD, never invalid UTF-8 */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":false,\"reason\":\"a\\ud83dz\"}}", &r) == 0);
    CHECK(strcmp(r.reason, "a\xEF\xBF\xBD" "z") == 0);
    /* surrogate pair */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":false,\"reason\":\"x\\ud83d\\ude00\"}}", &r) == 0);
    CHECK(strcmp(r.reason, "x\xF0\x9F\x98\x80") == 0);
    /* no reason given */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":false}}", &r) == 0 && r.ok == 0 && r.reason[0] != '\0');
}

static void test_login_not_understood(void) {
    LnOperatorLogin r;
    CHECK(ln_operator_parse_login(NULL, &r) == -1);
    CHECK(ln_operator_parse_login("", &r) == -1);
    CHECK(ln_operator_parse_login("<html>502 Bad Gateway</html>", &r) == -1);
    CHECK(ln_operator_parse_login("{\"exc_type\":\"PermissionError\"}", &r) == -1);
    CHECK(ln_operator_parse_login("{\"message\":\"just text\"}", &r) == -1);
    CHECK(ln_operator_parse_login("{\"message\":{\"reason\":\"no ok\"}}", &r) == -1);
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":\"true\",\"session\":\"t\"}}", &r) == -1);   /* a string, not true */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":truex,\"session\":\"t\"}}", &r) == -1);
    /* signed in but no token: never treated as signed in */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":true,\"operator_id\":\"OP1\"}}", &r) == -1 && r.ok == 0);
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":true,\"session\":\"\"}}", &r) == -1);
    /* truncated */
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":true,\"session\":\"abc", &r) == -1);
}

static void test_keys_inside_strings_are_ignored(void) {
    LnOperatorLogin r;
    /* a key that merely starts with the wanted one is not that key */
    CHECK(ln_operator_parse_login("{\"message\":{\"okay\":false,\"ok\":true,\"session\":\"x\"}}", &r) == 0 && r.ok == 1);
    CHECK(ln_operator_parse_login("{\"message\":{\"okay\":true,\"session\":\"x\"}}", &r) == -1);
    CHECK(ln_operator_parse_login("{\"message\":{\"ok\":true,\"sessions\":\"x\"}}", &r) == -1);
    /* ... and a key that is a prefix of the wanted one is not it either */
    CHECK(ln_operator_parse_login("{\"message\":{\"o\":true,\"ok\":false,\"reason\":\"r\"}}", &r) == 0 && r.ok == 0);
    /* a refusal whose text mentions "ok": true and "session" must stay a refusal with no token */
    CHECK(ln_operator_parse_login("{\"message\":{\"reason\":\"say \\\"ok\\\": true and \\\"session\\\": \\\"evil\\\"\",\"ok\":false}}", &r) == 0);
    CHECK(r.ok == 0 && r.session[0] == '\0');
    /* a "message" key nested deeper must not be picked up as the top-level one */
    CHECK(ln_operator_parse_login("{\"other\":{\"message\":{\"ok\":true,\"session\":\"evil\"}}}", &r) == -1);
    /* nested object before the real members */
    CHECK(ln_operator_parse_login("{\"message\":{\"meta\":{\"ok\":false,\"a\":[1,2,{\"b\":\"}\"}]},\"ok\":true,\"session\":\"good\"}}", &r) == 0);
    CHECK(r.ok == 1 && strcmp(r.session, "good") == 0);
}

static void test_truncation(void) {
    LnOperatorLogin r;
    char json[1200];
    char reason[700];
    memset(reason, 'a', sizeof(reason) - 1);
    reason[sizeof(reason) - 1] = '\0';
    snprintf(json, sizeof(json), "{\"message\":{\"ok\":false,\"reason\":\"%s\"}}", reason);
    CHECK(ln_operator_parse_login(json, &r) == 0);
    CHECK(strlen(r.reason) == sizeof(r.reason) - 1);
    /* a long token is cut but still terminated, never overrunning the field */
    memset(reason, 'z', sizeof(reason) - 1);
    snprintf(json, sizeof(json), "{\"message\":{\"ok\":true,\"session\":\"%s\"}}", reason);
    CHECK(ln_operator_parse_login(json, &r) == 0);
    CHECK(strlen(r.session) == sizeof(r.session) - 1);
}

static void test_pin_format(void) {
    CHECK(ln_operator_pin_format_ok("483920"));
    CHECK(ln_operator_pin_format_ok("48392011"));
    CHECK(!ln_operator_pin_format_ok("48392"));        /* 5 */
    CHECK(!ln_operator_pin_format_ok("483920112"));    /* 9 */
    CHECK(!ln_operator_pin_format_ok(""));
    CHECK(!ln_operator_pin_format_ok(NULL));
    CHECK(!ln_operator_pin_format_ok("48392a"));
    CHECK(!ln_operator_pin_format_ok("483 920"));
    CHECK(!ln_operator_pin_format_ok("\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xA5\xD9\xA6")); /* non-ASCII digits */
    CHECK(ln_operator_pin_format_ok("000000"));        /* shape only; the ERP decides weak */
}

static void test_response_rejected(void) {
    CHECK(ln_operator_response_rejected_signin("{\"message\":{\"verification_status\":\"Invalid\",\"verification_message\":\"Operator sign-in: The operator sign-in has expired. Sign in again.\"}}"));
    CHECK(!ln_operator_response_rejected_signin("{\"message\":{\"verification_status\":\"Attested\"}}"));
    CHECK(!ln_operator_response_rejected_signin(""));
    CHECK(!ln_operator_response_rejected_signin(NULL));
}

static void test_session_fresh(void) {
    time_t t0 = 1000000;
    CHECK(ln_operator_session_fresh(t0, t0, t0));
    CHECK(ln_operator_session_fresh(t0, t0, t0 + 24 * 60));
    CHECK(!ln_operator_session_fresh(t0, t0, t0 + 25 * 60));                       /* idle limit */
    CHECK(ln_operator_session_fresh(t0, t0 + 5 * 3600, t0 + 5 * 3600 + 60));       /* kept alive */
    CHECK(ln_operator_session_fresh(t0, t0 + 7 * 3600 + 40 * 60, t0 + 7 * 3600 + 49 * 60));
    CHECK(!ln_operator_session_fresh(t0, t0 + 7 * 3600 + 40 * 60, t0 + 7 * 3600 + 50 * 60)); /* absolute limit */
    CHECK(!ln_operator_session_fresh(0, 0, t0));                                   /* never signed in */
    CHECK(!ln_operator_session_fresh(0, 0, 100));                                  /* ... even at a tiny clock value */
    CHECK(!ln_operator_session_fresh(t0, t0, t0 - 1));                             /* clock went backwards */
    CHECK(!ln_operator_session_fresh(t0, t0 + 100, t0 + 50));
}

static void test_wipe(void) {
    char buf[16] = "secret-pin-12345"; /* 16 characters: no terminator to hide an unwiped last byte */
    ln_operator_wipe(buf, sizeof(buf));
    { size_t i; int zero = 1; for (i = 0; i < sizeof(buf); i++) if (buf[i]) zero = 0; CHECK(zero); }
    { char one[1] = {'x'}; ln_operator_wipe(one, 1); CHECK(one[0] == 0); }
    ln_operator_wipe(NULL, 5); /* no crash */
}

int main(void) {
    test_login_ok();
    test_login_refused();
    test_login_not_understood();
    test_keys_inside_strings_are_ignored();
    test_truncation();
    test_pin_format();
    test_response_rejected();
    test_session_fresh();
    test_wipe();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("ok\n");
    return 0;
}
