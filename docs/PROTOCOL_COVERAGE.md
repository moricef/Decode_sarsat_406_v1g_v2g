# COSPAS-SARSAT Protocol Coverage

This document tracks what the decoder actually supports after demodulation and
at the application layer. A BCH/CRC-valid frame must never be discarded only
because its protocol-specific fields are incomplete.

Reference documents used for this audit:

- C/S T.001 Issue 4 Rev.13, October 2025:
  `/home/fab2/Developpement/COSPAS-SARSAT/Docs/docs_COSPAS-SARSAT/T001-OCT-23-2025.pdf`
- C/S T.018 Issue 1 Rev.13, October 2025:
  `/home/fab2/Developpement/COSPAS-SARSAT/Docs/docs_COSPAS-SARSAT/T018-OCT-23-2025.pdf`

Status meanings:

- `decoded`: protocol is identified and meaningful fields are extracted.
- `raw`: protocol is identified and protected payload is preserved/displayed,
  but protocol-specific semantics are incomplete.
- `identified`: type is recognized, but output is not yet sufficient for a
  complete user-facing decode.
- `reserved`: value is reserved/spare by the standard or intentionally not
  decoded as an operational protocol.

## FGB / T.001

Decoder: `src/dec406_v1g.c`.

### Location Protocols, P=0, Long Frames

| Code | Protocol | Current status | Code path | Notes |
|---:|---|---|---|---|
| 0 | Spare | reserved | protocol map | Reported as unknown/spare. |
| 1 | Spare | reserved | protocol map | Reported as unknown/spare. |
| 2 | EPIRB MMSI Location | decoded | `decode_standard_location()` | Standard location branch. |
| 3 | ELT 24-bit Location | decoded | `decode_standard_location()` | Standard location branch. |
| 4 | ELT Serial Location | decoded | `decode_standard_location()` | Standard location branch. |
| 5 | ELT Operator Location | decoded | `decode_standard_location()` | Standard location branch. |
| 6 | EPIRB Serial Location | decoded | `decode_standard_location()` | Standard location branch. |
| 7 | PLB Serial Location | decoded | `decode_standard_location()` | Standard location branch. |
| 8 | National ELT Location | decoded | `decode_national_location()` | National fields partly interpreted. |
| 9 | ELT(DT) Location | decoded | `decode_elt_dt_location()` | Includes activation/freshness fields. |
| 10 | National EPIRB Location | decoded | `decode_national_location()` | National fields partly interpreted. |
| 11 | National PLB Location | decoded | `decode_national_location()` | National fields partly interpreted. |
| 12 | Ship Security | decoded | `decode_standard_location()` + security tag | Uses standard location with `[SECURITY]`. |
| 13 | RLS Location | decoded | `decode_rls_location()` | RLS fields are decoded. |
| 14 | Standard Test | raw | `decode_standard_test_data()` | Test payload is printed as bits/hex; semantics minimal. |
| 15 | National Test | raw | `decode_national_use_data()` | National data is printed raw-ish; semantics minimal. |

### User Protocols, P=1

| Code | Protocol | Current status | Code path | Notes |
|---:|---|---|---|---|
| 0 | Orbitography | decoded | `decode_orbitography_data()` | Used by calibration/orbitography beacons. |
| 1 | Aviation User | decoded | `display_baudot_2()` | Baudot call sign output. |
| 2 | Maritime User | decoded | `display_baudot_42()` / `display_specific_beacon()` | Baudot and specific beacon output. |
| 3 | Serial User | decoded | `decode_serial_user_protocol()` | Serial beacon type and serial fields. |
| 4 | National User | raw | `decode_national_use_data()` | Payload printed, national semantics incomplete. |
| 5 | Reserved for 2G | reserved | protocol map | Reported as unknown/reserved. |
| 6 | Radio Call Sign User | decoded | `decode_radio_callsign_data()` | Baudot call sign output. |
| 7 | Test User | raw | `decode_test_beacon_data()` | Test payload printed; should be labelled non-distress/self-test. |

### FGB Gaps

- Test protocols are recognized, but the user-facing output should explicitly
  say `TEST / SELF-TEST / NON-DISTRESS` where applicable.
- Raw protected frame bits should be printed for every CRC-valid FGB frame, so
  unsupported subfields remain inspectable.
- National protocol semantics are intentionally incomplete; current output is
  raw data plus broad protocol identification.

## SGB / T.018

Decoder: `src/dec406_v2g.c`.

Important channel-layer note: T.018 Table 2.2 defines separate PRN sequences
for normal and self-test mode. The active `freq_acq.c` and `despread.c` paths
now test and carry both PRN modes (`NORMAL` and `SELF-TEST`) through
acquisition, sync, despread, scanner display, and alert filtering.

### Main Field

| Field group | Current status | Code path | Notes |
|---|---|---|---|
| BCH(250,202) correction | decoded | `bch_decode_250_202()` | Corrects up to 6 bit errors; rejects degenerate all-zero/all-one locks. |
| 23 Hex ID | decoded | `compute_hex_id()` | Printed for every BCH-valid SGB. |
| TAC / serial / country | decoded | `decode_main()` | Printed in identification block. |
| Homing / RLS / test bit | decoded after BCH | `decode_main()` / `print_beacon_info()` | Test bit is shown as `Active (Non-operational)`; self-test PRN mode is also reported by the demodulator/scanner. |
| GNSS position | decoded | `decode_position()` | Default/no-position values handled. |
| Beacon type | decoded | `decode_main()` | ELT, EPIRB, PLB, ELT(DT), System Beacon. |
| Vessel/aircraft ID type 0 | decoded | `decode_vessel_id()` | None. |
| Vessel/aircraft ID type 1 | decoded | `decode_vessel_id()` | Maritime MMSI and AIS suffix. |
| Vessel/aircraft ID type 2 | decoded | `decode_vessel_id()` | Radio call sign. |
| Vessel/aircraft ID type 3 | decoded | `decode_vessel_id()` | Aircraft registration marking. |
| Vessel/aircraft ID type 4 | decoded | `decode_vessel_id()` | Aircraft 24-bit address and optional operator. |
| Vessel/aircraft ID type 5 | decoded | `decode_vessel_id()` | Aircraft operator + serial. |
| Vessel/aircraft ID type 6-7 | reserved | `decode_vessel_id()` | Reported as reserved type. |

### Rotating Fields

| ID | Rotating field | Current status | Code path | Notes |
|---:|---|---|---|---|
| 0 | G.008 Objective Requirements | decoded | `decode_rot_field()` | Elapsed time, last location, altitude, DOP, activation, battery, GNSS status. |
| 1 | In-Flight Emergency | decoded | `decode_rot_field()` | Time, altitude, triggering event. |
| 2 | RLS Acknowledgement | decoded | `decode_rot_field()` | RLS provider, capabilities, feedback, RLM data. |
| 3 | National Use | identified | default branch | T.018 Table 3.6: 44 national-use bits. Current code does not decode or print raw content. |
| 4 | Two-Way Communication | decoded | `decode_rot_field()` | Provider, DB version, ACK, questions/answers. |
| 5-14 | Spare for future use | reserved | default branch | Must still print raw rotating bits for inspection. |
| 15 | Cancellation Message | decoded | `decode_rot_field()` | Manual/automatic deactivation. |

### SGB Gaps

- For rotating field IDs not explicitly decoded, the decoder currently reports
  `Reserved/Spare`; it should also print the raw 48-bit rotating field.
- Rotating field #3 is a real National Use field, not spare. It needs a raw
  44-bit payload display at minimum.
- The corrected 250-bit codeword is already printed. Keep this behavior; it is
  the safety net for unknown or future protocol fields.
- Test/self-test is supported through bit 43 and through channel-layer PRN mode.
  The scanner suppresses alerts when the selected PRN is `SELF-TEST`.

## Implementation Rule

Application-layer incompleteness must never affect channel-layer acceptance:

1. FGB: CRC-valid frames are accepted, then protocol-specific decode runs.
2. SGB: BCH-valid frames are accepted, then protocol-specific decode runs.
3. Unknown, reserved, national, or future fields must be displayed as raw bits
   or hex, not hidden behind a generic `Unknown` message.
4. SGB acquisition must test all PRN modes required by T.018: normal and
   self-test.
