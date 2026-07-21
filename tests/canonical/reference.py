"""See README.md in this directory. Mirrors custody.py's canonical_json()
exactly (json.dumps with sort_keys=True, compact separators,
ensure_ascii=False, allow_nan=False) against the same test payloads as
test_canonical.c, in the same order.
"""

import json


def canonical_json(value):
    return json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    )


def base_payload(**overrides):
    payload = {
        "schema": "ln_tg_timing_result_v1",
        "station_version": "0.9.0",
        "station_id": "LN-TG-001",
        "operator_id": "",
        "identity_id": "",
        "work_order": "",
        "position": "",
        "qa_standard": "",
        "qa_rate_limit_s_per_day": 0,
        "temperature_condition": "",
        "measurement_type": "",
        "followup_mode": False,
        "timestamp_iso": "",
        "measurements": {},
        "baseline": {},
        "notes": "",
    }
    payload.update(overrides)
    return payload


# QA rate limits mirror ln_station_set_qa_standard()'s Observatory=2 /
# Signature=5 / Precision=10 / else=20 mapping.
QA_LIMITS = {"Observatory": 2, "Signature": 5, "Precision": 10}

# Case 1: ordinary values.
print(canonical_json(base_payload(
    operator_id="nick",
    identity_id="MOVEMENT-2892-00042",
    work_order="LN-MFG-00007",
    position="Dial Up",
    qa_standard="Precision",
    qa_rate_limit_s_per_day=QA_LIMITS["Precision"],
    temperature_condition="Room / 23C",
    measurement_type="Initial Certification",
    followup_mode=False,
    timestamp_iso="2026-07-21T12:00:00Z",
    measurements={
        "rate_s_per_day": 2.5,
        "beat_error_ms": 0.3,
        "amplitude_deg": 245.5,
        "beat_frequency_bph": 28800.0,
        "lift_angle_deg": 52.0,
        "sample_rate_hz": 44100.0,
        "duration_seconds": 20.0,
    },
    baseline={
        "has_baseline": True,
        "rate_s_per_day": -1.2,
        "beat_error_ms": 0.1,
        "amplitude_deg": 250.0,
    },
    notes="Test \"quoted\" and \\backslash\\ and line\nbreak",
)))

# Case 2: float precision edge cases.
print(canonical_json(base_payload(
    identity_id="X",
    position="Crown Up",
    qa_standard="Observatory",
    qa_rate_limit_s_per_day=QA_LIMITS["Observatory"],
    temperature_condition="Room / 23C",
    measurement_type="Follow-up Certification",
    followup_mode=True,
    timestamp_iso="2026-01-01T00:00:00Z",
    measurements={
        "rate_s_per_day": 0.1,
        "beat_error_ms": -0.0,
        "amplitude_deg": 1000.0,
        "beat_frequency_bph": 0.0,
        "lift_angle_deg": -52.75,
        "sample_rate_hz": 96000.0,
        "duration_seconds": 0.30000000000000004,
    },
    baseline={
        "has_baseline": False,
        "rate_s_per_day": 100000.0,
        "beat_error_ms": 0.0001,
        "amplitude_deg": -0.5,
    },
    notes="",
)))

# Case 3: UTF-8 passthrough and the scientific-notation boundary.
print(canonical_json(base_payload(
    identity_id="cafe-01",
    position="Dial Down",
    qa_standard="Signature",
    qa_rate_limit_s_per_day=QA_LIMITS["Signature"],
    temperature_condition="21°C",
    measurement_type="Service Check",
    followup_mode=False,
    timestamp_iso="2026-01-01T00:00:00Z",
    measurements={
        "rate_s_per_day": 999999.0,
        "beat_error_ms": 0.000001,
        "amplitude_deg": 359.999999,
        "beat_frequency_bph": 21600.0,
        "lift_angle_deg": 0.0,
        "sample_rate_hz": 22050.0,
        "duration_seconds": 1.0,
    },
    baseline={
        "has_baseline": True,
        "rate_s_per_day": 0.0,
        "beat_error_ms": 0.0,
        "amplitude_deg": 0.0,
    },
    notes="naïve café ✓",
)))
