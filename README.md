# dec406 — COSPAS-SARSAT 406 MHz Beacon Decoder

Decoder for 1st and 2nd generation COSPAS-SARSAT emergency beacons (406 MHz).

**Status**: Active development (May 2026)
**Branch**: `feature/no-matched-filter`

---

## What works

### Decoders (bit-perfect)
- **1G (FGB)**: Biphase-L, 400 bps, Location/User protocols — `dec406_v1g.c`
- **2G (SGB)**: BCH(250,202) Berlekamp-Massey + Chien, all T.018 fields — `dec406_v2g.c`

### DSSS OQPSK Demodulator (2G)
- ✅ Synthetic signal (same TX/RX clock): bit-perfect
- 🟡 Synthetic + 8.5 kHz offset: FFT finds the offset, preamble sync OK (~95%), BCH errors on message (residual phase drift)
- ❌ Over-the-air (Pluto → RTL-SDR): not yet working

---

## Build

```bash
make
```

Binaries produced in `build/`:

| Binary | Purpose |
|--------|---------|
| `dec406_iq` | DSSS OQPSK demodulator from IQ file (float32 complex, 2.4576 MHz) |
| `dec406_hex` | 1G/2G decoder from hex string |
| `dec406_audio` | 1G decoder from WAV file |
| `dec406_dsss_test` | DSSS demodulator test harness |
| `generate_2g_hex` | 2G test frame generator |
| `resample_iq` | IQ file resampler (libsamplerate) |
| `reset_usb` | USB reset utility for scan406.pl |

---

## Usage

```bash
# Demodulate a 2G IQ file
./build/dec406_iq signal.iq -s 2457600

# Decode a 2G hex frame (63 chars = 252 bits)
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4

# Decode a 1G hex frame (28 chars = 112 bits)
./build/dec406_hex 8E3301E2402B002BBA863609670908

# Generate a test IQ signal with the PlutoSDR
./bin/sarsat_sgb -o test.iq
```

---

## 2G Demodulation Chain

```
IQ @ 2.4576 MHz
  → Q delay by 32 samples (OQPSK compensation)
  → FFT coarse frequency estimator (±25 kHz, 64 phases × 4 Costas ambiguities)
  → frequency correction (if offset above threshold)
  → RRC matched filter (α=0.5, 705 taps)
  → decimation (SPS=64) + symbol sync
  → Costas QPSK loop or bypass
  → DSSS despread (2-pass preamble sync, per-bit majority over 256 chips)
  → 250 bits → decode_2g() → BCH(250,202)
```

---

## Project Structure

```
src/         C sources (dsss_demod, rrc_filter, symbol_sync, costas4, despread,
              dec406_v1g, dec406_v2g, main_iq, etc.)
include/     Headers (despread.h, dsss_demod.h, rrc_filter.h, etc.)
build/       Compiled binaries
tests/       test_sgb_codec.c (94 tests: BCH, PRN, message)
scripts/     scan406.pl, test_prn.c, test_rrc.c
utils/       resample_iq, generate_2g_hex, reset_usb
data/        Test IQ files
docs/        T.018 specifications, validation procedures, archives
```

---

## Documentation

- `ARCHITECTURE_dec406.md` — Detailed architecture, current status, known issues (French)
- `ARCHITECTURE_dec406.html` — Same document as HTML
- `docs/TESTS_VALIDATION.md` — Validation procedures (unit, cross-codec, AWGN) (French)
- `docs/2024/` — T.018 specification extracts (physical layer, message format, PRN, BCH)
- `docs/archives/` — Obsolete documentation (2025 sessions, MATLAB Coder)

---

## References

- **C/S T.001** — 1st Generation Beacon Specification (FGB)
- **C/S T.018** — 2nd Generation Beacon Specification (SGB)
- `gr-cospas` — GNU Radio BPSK demodulator (1G)
- `sgb-codec` (jbirby) — Python reference SGB codec
- `sarsat_sgb` (ADALM-PLUTO) — SGB modulator for PlutoSDR
