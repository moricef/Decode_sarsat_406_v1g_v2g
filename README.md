# dec406 — COSPAS-SARSAT 406 MHz Beacon Decoder

Decoder for 1st and 2nd generation COSPAS-SARSAT emergency beacons (406 MHz).

**Status**: Active development (May 2026)
**Branch**: `feature/fll-pll-tracking`

---

## What works

### Decoders (bit-perfect)
- **1G (FGB)**: Biphase-L, 400 bps, Location/User protocols — `dec406_v1g.c`
- **2G (SGB)**: BCH(250,202) Berlekamp-Massey + Chien, all T.018 fields — `dec406_v2g.c`

### DSSS OQPSK Demodulator (2G)
- ✅ Synthetic signal (same TX/RX clock): bit-perfect
- ✅ Over-the-air (Pluto → RTL-SDR / SDRangel): decodes, BCH-clean
- 🟡 Weak-link limit: at ~2.5 m antenna spacing the burst still decodes down
  to roughly -30 dB Pluto TX gain; below that the DSSS correlation collapses
  into the noise floor (link budget, not a decoder limit)

---

## Build

```bash
make build/dec406_iq
```

Binaries produced in `build/`:

| Binary | Purpose |
|--------|---------|
| `dec406_iq` | DSSS OQPSK demodulator from IQ file (2.4576 MHz) |
| `dec406_hex` | 1G/2G decoder from hex string |
| `dec406_audio` | 1G decoder from WAV file |
| `generate_2g_hex` | 2G test frame generator |
| `resample_iq` | IQ file resampler (libsamplerate) |

---

## Usage

```bash
# Demodulate a 2G IQ file (float32 complex, default)
./build/dec406_iq signal.iq -s 2457600

# SDRangel ci32_le recording (int32 complex little-endian)
./build/dec406_iq recording.sigmf-data -s 2457600 -I

# RTL-SDR uint8 recording
./build/dec406_iq recording.iq -s 2457600 -u

# Decode a 2G hex frame (63 chars = 252 bits)
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4

# Decode a 1G hex frame (28 chars = 112 bits)
./build/dec406_hex 8E3301E2402B002BBA863609670908
```

Input formats: float32 complex (default), `-u` RTL-SDR uint8, `-i` int16
interleaved, `-I` int32 SDRangel ci32_le.

---

## 2G Demodulation Chain

```
IQ @ 2.4576 MHz
  → DC blocker (IIR, α=0.001) on raw samples
  → coarse frequency acquisition: 4th-power FFT (±25 kHz sanity check)
       fallback: ±300 Hz PRN-correlation sweep
  → OQPSK delay (Q advanced by SPS/2 = 32 samples)
  → tracking loop (FLL+PLL+DLL+Kalman): sample-rate carrier tracking
       + decimation in a single pass → chip-rate output
  → despread (2-pass preamble sync, complex 4-phase correlation,
       soft per-bit correlation over 256 chips)
  → 250 bits → decode_2g() → BCH(250,202)
```

`main_iq.c` scans the file with a 1.35 s sliding window (0.25 s step), keeps
the highest-quality burst (preamble z-score) and decodes it. A burst that
correlates below the sync threshold, or whose codeword BCH cannot correct, is
rejected — no beacon is printed from noise.

---

## Project Structure

```
src/         C sources (dsss_demod, tracking, kalman5, freq_acq, despread,
              dec406_v1g, dec406_v2g, main_iq, etc.)
include/     Headers (despread.h, dsss_demod.h, tracking.h, kalman5.h, ...)
build/       Compiled binaries
tests/       test_sgb_codec.c (94 unit tests), test_bch_reject.c
scripts/     Analysis / debug scripts (Python)
docs/        T.018 specifications, architecture, validation (not on GitHub)
```

---

## Documentation

- `docs/ARCHITECTURE_dec406.md` — Detailed architecture, current status (French)
- `docs/ARCHITECTURE_dec406.html` — Same document as HTML
- `docs/TESTS_VALIDATION.md` — Validation procedures (unit, cross-codec, AWGN, OTA)
- `CHANGELOG.md` — Version history

---

## References

- **C/S T.001** — 1st Generation Beacon Specification (FGB)
- **C/S T.018** — 2nd Generation Beacon Specification (SGB)
- `gr-cospas` — GNU Radio BPSK demodulator (1G)
- `sgb-codec` (jbirby) — Python reference SGB codec
- `sarsat_sgb` (ADALM-PLUTO) — SGB modulator for PlutoSDR
