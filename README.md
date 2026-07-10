# LNWS Milestone 0.8 Certification Profiles + Follow-up Context

Adds certification-oriented timing context.

## QA profiles

- Workshop: +/- 20 s/day
- Precision: +/- 10 s/day
- Signature: +/- 5 s/day
- Observatory: +/- 2 s/day

## Temperature condition selector

- Room / 23C
- Cold / 8C
- Warm / 38C
- Custom

This aligns the workflow with COSC-style multi-temperature logging without claiming COSC certification.

## Measurement type selector

- Initial Certification
- Follow-up Certification
- Service Check
- Regulation Check

Follow-up mode is now represented in station state and JSON export.

## Baseline comparison scaffolding

The station can store baseline readings per position and compare follow-up readings against the original result. For now, baseline data is a local API placeholder; the next milestone should populate it from ERP when an identity is scanned.

## Data bar

The data bar still shows:

```text
25200 bph  pos Dial Up check
```

In follow-up mode, if a baseline is present, it also shows rate delta:

```text
25200 bph  pos Dial Up check d+1.2
```

## JSON export additions

- qa_standard
- qa_rate_limit_s_per_day
- temperature_condition
- measurement_type
- followup_mode
- baseline

## Build

```bash
make clean
./autogen.sh
./configure
make
```
