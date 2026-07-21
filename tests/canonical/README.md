# Canonical JSON regression test

`ln_canonical_timing_result_json()` must produce byte-for-byte the same
output as the ERP's `canonical_json()` (Python's `json.dumps(...,
sort_keys=True, separators=(",", ":"))`) for the same field values. If the
two ever disagree, every workstation signature silently verifies as
"Invalid" on the server -- there is no partial-credit failure mode, and
nothing in either program raises an error when this happens.

This directory is a standalone check, not wired into the autotools build
(it needs no GTK/portaudio/curl, just glib). Run it after any change to
`src/ln_canonical.c`:

```bash
# from the Ln-tg repo root, in an MSYS2 UCRT64 shell (or any shell with
# gcc + pkg-config + glib-2.0 on PATH)
gcc -I../../src $(pkg-config --cflags glib-2.0) -o /tmp/test_canonical \
    test_canonical.c ../../src/ln_canonical.c ../../src/ln_station.c \
    $(pkg-config --libs glib-2.0) -lcurl

/tmp/test_canonical > c_output.txt
python3 reference.py > python_output.txt
diff c_output.txt python_output.txt && echo "MATCH"
```

Both programs emit one canonical-JSON line per test case, in the same
order, covering the cases that have actually caught real bugs in this
module during development:

1. **Ordinary values** -- everyday rate/amplitude/etc. readings.
2. **Float precision edge cases** -- negative zero, `0.1`, the classic
   `0.1 + 0.2` artifact (`0.30000000000000004`), and a "round" whole
   number (`20.0`) -- an earlier draft used `%g` for shortest-round-trip
   search and silently rendered `20.0` as `2e+01`, because `%g`'s own
   scientific-notation threshold moves with the search precision.
3. **UTF-8 passthrough and the scientific-notation boundary** --
   non-ASCII text (the server uses `ensure_ascii=False`) and a small
   value (`0.000001`) that Python's `repr()` renders as `1e-06` -- an
   earlier draft used fixed-point formatting exclusively and never
   produced scientific notation at all, which would disagree with Python
   for any realistically-occurring near-zero measurement (e.g. a very
   well-regulated watch's rate).

The scientific-vs-fixed threshold implemented in
`ln_canonical_format_double()` was confirmed directly against CPython's
own source (`Python/pystrtod.c`, `format_code == 'r'`:
`decpt <= -4 || decpt > 16`), not guessed.
