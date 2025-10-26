# dec406 - COSPAS-SARSAT 406 MHz Beacon Decoder

Decoder for COSPAS-SARSAT 1st and 2nd generation emergency beacons (406 MHz).

**Status**: Production-ready (October 2025)
**License**: Creative Commons CC BY-NC-SA

---

## Features

### First Generation (1G) Beacons
- **Protocols**: Location (Standard, National, ELT-DT, RLS), User (Orbitography, Aviation, Maritime, Serial)
- **Modulation**: Biphase-L (Manchester), 400 bps
- **Position**: GPS coordinates with 3.4m resolution
- **Validation**: CRC error detection

### Second Generation (2G) Beacons
- **Protocols**: All T.018 rotating fields (RF#0-15)
- **Modulation**: OQPSK DSSS, 300 bps
- **Error Correction**: BCH(250,202) - T.018 Appendix B compliant
- **Position**: High-precision GPS (3.4m resolution)
- **Advanced Features**: RLS Type 1/2/3, Two-Way Communication, G.008 Objective Requirements

**Latest Update (2025-10-26)**: Critical fix - Phase 1 ambiguity resolution (3 major bugs corrected).
**Previous Update (2025-10-17)**: Fixed BCH verification algorithm to full T.018 compliance.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Input Sources                         │
├───────────────────────────┬─────────────────────────────┤
│       WAV Audio (1G)      │      Hex String (1G/2G)     │
└────────────┬──────────────┴──────────────┬──────────────┘
             │                              │
             ▼                              ▼
┌─────────────────────────────────────────────────────────┐
│              Demodulator / Parser                        │
│  • audio_capture.c (Biphase-L from audio)               │
│  • dec406_main.c (hex string parser)                    │
└────────────────────┬────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│                Frame Decoders                            │
│  • dec406_v1g.c → 1G protocols (CRC validation)         │
│  • dec406_v2g.c → 2G protocols (BCH correction)         │
└────────────────────┬────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────┐
│                Decoded Output                            │
│  • Beacon ID (15/23 Hex), Type, Country (MID)           │
│  • GPS Position, Timestamp                              │
│  • Vessel/Aircraft ID (MMSI, Call Sign, ICAO24)         │
│  • OpenStreetMap link for position visualization        │
└─────────────────────────────────────────────────────────┘
```

---

## Compilation

```bash
# Compile all binaries
gcc -o dec406_hex dec406_main.c dec406.c dec406_v1g.c dec406_v2g.c display_utils.c -lm -O2
gcc -o dec406_audio main_audio.c audio_capture.c dec406.c dec406_v1g.c dec406_v2g.c display_utils.c -lm -O2
gcc -o generate_2g_hex generate_2g_hex.c -lm
```

**Dependencies**: Standard C library, math library (`-lm`)

---

## Usage

### Decode from Hex String

```bash
./dec406_hex <hex_string>

# Example 1G frame (112 bits = 28 hex chars)
./dec406_hex 8E3301E2402B002BBA863609670908

# Example 2G frame (252 bits = 63 hex chars)
./dec406_hex 89C3F45639195999A02B33326C3EC4400007FFF00C0283200000DCA2C07A361
```

**Output**:
```
=== 406 MHz SECOND GENERATION BEACON (SGB) ===
[IDENTIFICATION]
 23 Hex ID: 9C949C3F4569361F6220000
 TAC Number: 9999 (0x270F)
 Serial Number: 13398 (0x3456)
 Country Code: 228 (France)
 Type: EPIRB
 Vessel ID: MMSI:227006600

[STATUS]
 Test Protocol: Active (Non-operational)
 RLS Capability: Enabled

[ENCODED GNSS LOCATION]
 Position: 43.20001°N, 5.39999°E
 Coordinates: 43.20001°N, 5.39999°E
 Resolution: ~3.4 meters maximum

[ROTATING FIELD 0]
 Type: G.008 Objective Requirements
 Elapsed time: 3 hours
 Last position: 5 minutes ago
 Altitude: 0 meters
```

### Decode from WAV File

```bash
./dec406_audio <audio_file.wav>

# Capture from RTL-SDR and decode
rtl_fm -f 406.028M -s 48k -g 40 | sox -t raw -r 48k -e s -b 16 -c 1 - output.wav
./dec406_audio output.wav
```

### Generate Test Frames

```bash
# Generate 2G test frames
./generate_2g_hex
./generate_2g_fixed
```

---

## Validation

### Test Frames (Verified)

**1G Frame** (EPIRB France):
```
8E3301E2402B002BBA863609670908
```

**2G Frames** (from dsPIC33CK hardware beacon, T.018 compliant):
```
Frame 1: 89C3F45639195999A02B33326C3EC4400007FFF00C0283200000DCA2C07A361
  ✓ BCH valid (syndrome = 0x0000000000000)
  France, EPIRB, TAC:9999, 43.20°N 5.40°E

Frame 2: 0C0E7456390956CCD02799A2468ACF135787FFF00C02832000037707609BC0F
  ✓ BCH valid (syndrome = 0x0000000000000)
  France, EPIRB, TAC:12345, 42.85°N 4.95°E
```

Both frames validated against:
- T.018 BCH(250,202) specification (Appendix B)
- External BCH validator (independent verification)
- Hardware reference implementation (dsPIC33CK)

---

## Technical Specifications

### 1G Beacons (C/S T.001)
- **Frequency**: 406.0 - 406.1 MHz
- **Modulation**: Biphase-L (Manchester)
- **Data Rate**: 400 bps
- **Frame Length**: 112 bits (short), 144 bits (long)
- **Error Detection**: CRC

### 2G Beacons (C/S T.018)
- **Frequency**: 406.0 - 406.1 MHz
- **Modulation**: OQPSK with DSSS (256 chips/bit)
- **Data Rate**: 300 bps
- **Frame Length**: 250 bits (data) + 2 bits (header)
- **Error Correction**: BCH(250,202) with 48-bit parity
- **Generator Polynomial**: `g(x) = 0x1C7EB85DF3C97` (49 bits)

---

## Project Structure

```
dec406_v10.2/
├── dec406_v1g.c          # 1G frame decoder (55K)
├── dec406_v2g.c          # 2G frame decoder with BCH (34K)
├── dec406_main.c         # Hex string entry point
├── main_audio.c          # WAV file entry point
├── audio_capture.c       # Biphase-L demodulator
├── display_utils.c       # Output formatting, OSM links
├── country_codes.h       # MID database (10-bit country codes)
├── generate_2g_*.c       # Test frame generators
└── Docs/                 # Technical documentation
```

---

## Recent Changes

### 2025-10-26: Phase 1 Ambiguity Resolution - Critical Fix (3 Bugs)
**Commit**: 29a0661

**Bug #1 - Incorrect Preamble Pattern**:
- Phase 1 was testing Q channel against all 1s instead of all 0s (T.018 §2.2.4)
- Impact: Wrong pattern caused 38% errors on preamble decoding
- Fix: Both I and Q channels now correctly expect all 0s

**Bug #2 - Insufficient Test Sample Size**:
- Phase 1 tested only 50 chips instead of 12,800 (0.4% of preamble!)
- Impact: False 89% correlation on tiny sample, wrong parameter selection
- Fix: Now tests full preamble (50 bits × 256 chips/bit = 12,800 chips)

**Bug #3 - OQPSK Tc/2 Delay Inconsistency**:
- `apply_oqpsk_delay_for_corr` delayed Q instead of advancing it
- Impact: Inconsistent with `oqpsk_to_qpsk`, caused correlation errors
- Fix: Both functions now consistently compensate TX OQPSK delay

**Results**:
- Diagnostic correlation improved from 62% → 68%
- Phase detection now realistic: 50.6% on full preamble (was false 89%)
- Parameter consistency achieved between all demod stages
- **Remaining issue**: Despread still shows 3.2% avg (investigation ongoing)

**Details**: See `Docs/DEBUGGING_SESSION_2025-10-26.md`

---

### 2025-10-17: BCH Algorithm Fix
**Problem**: False BCH errors reported on valid frames from hardware beacons.

**Root Cause**: Incorrect BCH syndrome calculation (missing 5 padding zeros per T.018 Appendix B).

**Solution**: Reimplemented BCH verification with correct 255-bit codeword construction:
- 5 padding zeros + 202 data bits + 48 BCH parity bits = 255 bits
- Standard modulo-2 polynomial long division
- Correct generator polynomial: `0x1C7EB85DF3C97`

**Result**: 100% validation success on hardware-generated frames.

---

## Authors

- **Original dec406_v7**: F4EHY (2020)
- **v10.2 Refactoring & 2G Support**: Collaborative development with Claude Code (2025)
- **T.018 Compliance**: Full BCH implementation, MID database, rotating fields

---

## References

- **C/S T.001**: COSPAS-SARSAT 1st Generation Beacon Specification
- **C/S T.018**: COSPAS-SARSAT 2nd Generation Beacon Specification (SGB)
- **C/S G.005**: MEOSAR Return Link Service
- **Hardware Reference**: dsPIC33CK T.018 compliant beacon implementation

---

## Notes

- For **IQ direct demodulation**, use [gr-cospas](https://github.com/moricef/gr-cospas) (GNU Radio OOT module)
- This decoder focuses on **frame decoding** after demodulation
- Audio input uses Biphase-L demodulation via `audio_capture.c`
- Hex input bypasses demodulation (for testing or external demodulators)

---

**Repository**: https://github.com/moricef/Decode_sarsat_406_v1g_v2g
