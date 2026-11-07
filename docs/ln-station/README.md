# LN Timegrapher Station Design

## Workflow

1. Scan or type an `LN Inventory Identity`.
2. Run the timegrapher normally.
3. Save a timing result.
4. Export JSON locally.
5. Later: POST result to LN Watch Inventory.

## Offline-first

The station should still work without ERP connectivity. This matters because a timegrapher is bench equipment first and ERP equipment second.

## Payload target

Future endpoint in LN Watch Inventory:

```text
/api/method/ln_watch_inventory.api.timegrapher.submit_timing_result
```

## Certification data

The JSON payload is designed to map into:

- LN Timing Session
- LN Timing Position Result
- LN Identity Event
- LN Certificate
- LN Watch Passport

## Operator sign-in

The station signs each result as its Workstation but has no key for the operator, so by itself every result
arrives at the ERP as `Invalid` (one of the two required signatures). With a PIN sign-in the ERP vouches for
the operator instead and records the result as `Attested`.

1. Next to **Operator**, press **Sign in** and enter the Operator ID and PIN (6 to 8 digits). An administrator
   sets the PIN in the ERP. The station must be registered there (LN Workstation, with its API User).
2. The ERP answers with a session token. The station keeps it **in memory only** (never in the config, a log or
   a JSON export) and sends it with each result as `operator_session`, outside the data the station signs.
3. The Operator field is read-only while signed in. **Sign out** ends the session.
4. A sign-in lasts 8 hours, or 30 minutes without a result. The station stops using a token slightly before
   that, and **Complete Session** asks for a new sign-in first rather than submitting results that would be
   `Invalid`. With nobody signed in, results are submitted as before, unattested.

A wrong PIN counts against the operator: five wrong PINs lock them for 15 minutes. A PIN of the wrong length is
refused on the station without being sent, so it does not use up a try. The protocol is in the ERP repo:
`docs/architecture/operator_pin_and_attestation.md`. Code: `src/ln_operator.c` (pure helpers),
`ln_station_operator_*` in `src/ln_station.c`, the dialog in `src/ln_station_panel.c`. Tests: `tests/operator`.
