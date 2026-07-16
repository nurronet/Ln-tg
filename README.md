# LNWS Milestone 0.10.1 ERP Search Diagnostics

Adds visible diagnostics to the smart identity search.

## New status messages

- search queued with the 350 ms debounce
- exact query being searched
- endpoint name and 10-second timeout
- response time in milliseconds
- number of autocomplete matches parsed
- explicit no-results message with a response excerpt
- HTTP status and ERP response body on server errors
- network/TLS/timeout details on libcurl failures

The same diagnostics are also printed to the terminal with prefixes:

```text
[LNWS ERP]
[LNWS SEARCH]
```

No API key or secret is printed.

## Files changed

- `src/ln_erp_config.c`
- `src/ln_station_panel.c`

The other files are included unchanged so this package can replace the full
Milestone 0.10 modified-file set.

## Build

```bash
make clean
./autogen.sh
./configure
make -j1
```

## Expected examples

```text
ERP search complete: 2 matches in 184 ms. Use arrow keys or click a result.
```

```text
ERP search timed out after 10.0 s. Check server reachability, TLS, or endpoint deployment.
```

```text
ERP search failed: code 417 after 220 ms. Detail: { ... }
```
