# Operator sign-in tests

Standalone checks, not wired into the autotools build. They need glib, libcurl and libsodium (no GTK).

**Unit tests** for `src/ln_operator.c` (reading the ERP's answer, PIN shape, expiry rule):

```bash
gcc -Wall -Wextra -I../../src -o test_operator test_operator.c ../../src/ln_operator.c && ./test_operator
```

**Flow test**: `run_flow_test.py` starts a fake ERP on localhost, runs `flow_harness.c` (the real
`ln_station_operator_*` and `ln_station_submit_result` code) against it, and checks what went over the wire
and what reached disk: the PIN and token must appear only in requests to the ERP, never in the export file or
the submit log, and a stale or rejected sign-in must not be sent.

```bash
# from the repo root, in an MSYS2 UCRT64 shell
gcc -Wall -Wextra -Isrc $(pkg-config --cflags glib-2.0 libsodium) -o flow_harness tests/operator/flow_harness.c \
    src/ln_station.c src/ln_operator.c src/ln_canonical.c src/ln_signing.c \
    $(pkg-config --libs glib-2.0 libsodium libcurl)
python tests/operator/run_flow_test.py ./flow_harness
```

The script sets `XDG_CONFIG_HOME` to a temporary directory so the station key it generates does not land in
your real user config.

If the program dies at start with status `0xC0000139`, an older OpenSSL in `C:\Windows\System32`
(`libssl-3-x64.dll`, `libcrypto-3-x64.dll`) is being loaded instead of the MSYS2 one. Copy the two DLLs from
`C:\msys64\ucrt64\bin` next to the test executable.
