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
