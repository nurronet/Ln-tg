#ifndef LN_CANONICAL_H
#define LN_CANONICAL_H

#include <glib.h>

#include "ln_station.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the LN-CANONICAL-JSON-1 form of a timing result payload: sorted
 * object keys (recursively), compact separators, UTF-8 passthrough, and
 * number formatting that matches Python's json.dumps() byte-for-byte for
 * every value this app can produce. This must never include a
 * "signatures" field -- it is exactly the byte sequence that gets signed
 * on the client and independently re-derived by the ERP from the parsed
 * (signature-stripped) payload it receives. If the two sides ever
 * disagree on these bytes, every signature silently verifies as
 * "Invalid" -- there is no partial-credit failure mode.
 *
 * Caller owns the returned GString (g_string_free(result, TRUE)).
 */
GString *ln_canonical_timing_result_json(const LnStationContext *ctx, const LnTimingResult *result);

/* Formats a double exactly the way Python's json.dumps() does: the
 * shortest decimal string that round-trips to the same IEEE-754 double,
 * always including a decimal point (Python never emits a bare integer
 * for a float value, e.g. 5.0 not 5). Returns the number of characters
 * written (excluding the NUL terminator). Exposed for testing.
 */
int ln_canonical_format_double(double value, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif
