#include "ln_operator.h"

#include <string.h>

/* A small reader for the ERP's JSON answers. It walks the text member by
 * member and skips whole values, so a key that happens to appear inside a
 * string value (a reason that mentions "session") is never mistaken for
 * the key itself. */

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* p is at an opening quote. Returns the character after the closing quote, or NULL. */
static const char *skip_string(const char *p) {
    if (*p != '"') return NULL;
    p++;
    while (*p) {
        if (*p == '\\') {
            if (!p[1]) return NULL;
            p += 2;
        } else if (*p == '"') {
            return p + 1;
        } else {
            p++;
        }
    }
    return NULL;
}

/* p is at the start of a value. Returns the character after it, or NULL. */
static const char *skip_value(const char *p) {
    p = skip_ws(p);
    if (*p == '"') return skip_string(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = skip_string(p);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') depth++;
            if (*p == '}' || *p == ']') {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    if (!*p) return NULL;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    return p;
}

/* p is at '{'. Returns a pointer to the value of the member named `key`, or NULL. */
static const char *find_member(const char *p, const char *key) {
    size_t key_len = strlen(key);
    p = skip_ws(p);
    if (*p != '{') return NULL;
    p++;
    for (;;) {
        const char *name;
        const char *name_end;
        p = skip_ws(p);
        if (*p != '"') return NULL; /* '}' (no more members) or garbage */
        name = p + 1;
        name_end = skip_string(p);
        if (!name_end) return NULL;
        p = skip_ws(name_end);
        if (*p != ':') return NULL;
        p = skip_ws(p + 1);
        if ((size_t)(name_end - 1 - name) == key_len && memcmp(name, key, key_len) == 0) return p;
        p = skip_value(p);
        if (!p) return NULL;
        p = skip_ws(p);
        if (*p == ',') p++;
        else return NULL;
    }
}

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static size_t put_utf8(char *out, size_t room, unsigned cp) {
    if (cp < 0x80) {
        if (room < 1) return 0;
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (room < 2) return 0;
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (room < 3) return 0;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (room < 4) return 0;
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* p is at an opening quote. Copies the unescaped text into out (cut short,
 * never mid-character, if it does not fit). Returns 0, or -1 if p is not a
 * well-formed string. */
static int read_string(const char *p, char *out, size_t out_len) {
    size_t n = 0;
    if (*p != '"' || out_len == 0) return -1;
    p++;
    for (;;) {
        char c = *p;
        char buf[4];
        size_t w = 0;
        if (!c) return -1;
        if (c == '"') break;
        if (c == '\\') {
            char e = p[1];
            p += 2;
            switch (e) {
                case '"': buf[0] = '"'; w = 1; break;
                case '\\': buf[0] = '\\'; w = 1; break;
                case '/': buf[0] = '/'; w = 1; break;
                case 'b': buf[0] = '\b'; w = 1; break;
                case 'f': buf[0] = '\f'; w = 1; break;
                case 'n': buf[0] = '\n'; w = 1; break;
                case 'r': buf[0] = '\r'; w = 1; break;
                case 't': buf[0] = '\t'; w = 1; break;
                case 'u': {
                    unsigned cp, low;
                    char tmp[4];
                    if (hex4(p, &cp) != 0) return -1;
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u' && hex4(p + 2, &low) == 0 &&
                        low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        p += 6;
                    } else if (cp >= 0xD800 && cp < 0xE000) {
                        cp = 0xFFFD; /* a lone surrogate */
                    }
                    w = put_utf8(tmp, sizeof(tmp), cp);
                    memcpy(buf, tmp, w);
                    break;
                }
                default: return -1;
            }
        } else {
            buf[0] = c;
            w = 1;
            p++;
        }
        if (n + w + 1 > out_len) break; /* full: keep what fits (whole characters only) */
        memcpy(out + n, buf, w);
        n += w;
    }
    out[n] = '\0';
    return 0;
}

static int read_string_member(const char *obj, const char *key, char *out, size_t out_len) {
    const char *v = find_member(obj, key);
    if (!v) return -1;
    return read_string(v, out, out_len);
}

int ln_operator_parse_login(const char *json, LnOperatorLogin *out) {
    const char *message;
    const char *ok;

    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!json) return -1;

    message = find_member(json, "message");
    if (!message) return -1;
    ok = find_member(message, "ok");
    if (!ok) return -1;

    if (strncmp(ok, "true", 4) == 0 && (ok[4] == '\0' || strchr(",} \t\r\n", ok[4]))) {
        if (read_string_member(message, "session", out->session, sizeof(out->session)) != 0 || !out->session[0]) {
            ln_operator_wipe(out, sizeof(*out));
            return -1;
        }
        read_string_member(message, "operator_id", out->operator_id, sizeof(out->operator_id));
        read_string_member(message, "operator_name", out->operator_name, sizeof(out->operator_name));
        out->ok = 1;
        return 0;
    }
    if (strncmp(ok, "false", 5) == 0 && (ok[5] == '\0' || strchr(",} \t\r\n", ok[5]))) {
        if (read_string_member(message, "reason", out->reason, sizeof(out->reason)) != 0 || !out->reason[0])
            strcpy(out->reason, "The ERP refused the sign-in.");
        out->ok = 0;
        return 0;
    }
    return -1;
}

int ln_operator_pin_format_ok(const char *pin) {
    size_t n = 0;
    if (!pin) return 0;
    for (; pin[n]; n++) {
        if (pin[n] < '0' || pin[n] > '9') return 0;
        if (n >= 8) return 0;
    }
    return n >= 6;
}

int ln_operator_response_rejected_signin(const char *response) {
    return response && strstr(response, "Operator sign-in:") != NULL;
}

int ln_operator_session_fresh(time_t signed_at, time_t last_used, time_t now) {
    if (signed_at == 0) return 0;
    if (now < signed_at || now < last_used) return 0; /* the clock went backwards: do not trust it */
    if (now - signed_at >= LN_OPERATOR_MAX_SECONDS) return 0;
    if (now - last_used >= LN_OPERATOR_IDLE_SECONDS) return 0;
    return 1;
}

void ln_operator_wipe(void *p, size_t n) {
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (p && n--) *v++ = 0;
}
