# Changelog - dec406_v10.2

## Version 10.2.12 - 2026-07-12 - SGB TWC long-answer decoding

The SGB rotating-field #4 decoder now honors the T.018 Answer Format Flag.
Short-format TWC messages retain their three 7-bit question plus 4-bit answer
slots. Long-format messages are decoded as one 7-bit question with a 15-bit
answer bitmap followed by one short 7+4-bit slot, as specified by C/S T.018
Issue 1 Revision 13, Table 3.7.

Validation used BCH-valid synthetic short and long RF#4 frames through
`dec406_hex`. The main decoder and scanner builds passed, as did the mandatory
synthetic SGB regression with zero BCH errors.

Cancellation rotating-field #15 decoding was also aligned with T.018 Table
3.9: value `01` now reports automatic deactivation by external means and
value `10` reports manual deactivation by the user.

## Version 10.2.11 - 2026-07-08 - Scanner decode worker and FGB BCH-2

### Decode worker for real-time scanning

FGB and SGB decoding now runs in a dedicated worker fed by a bounded queue.
The scanner copies each detected IQ burst immediately and resumes spectral
detection while the worker performs carrier wipeoff and decoding. Heartbeat
logs report both SDR ring overruns and decode queue drops.

Field validation on firmin decoded 14 SGB self-test frames and 2 normal frames
during trains spaced as closely as one second, with all frames passing BCH and
both counters remaining at zero. A separate hardware test decoded four frames
in five seconds without loss.

A longer firmin run on 2026-07-09 decoded all 450 detected SGB bursts
(252 self-test, 198 normal), with zero frame rejects, zero decode drops and
zero real ring overruns.

### FGB BCH-2 correction

Long non-orbitography FGB frames now receive BCH(38,26) correction over bits
107-144, recovering up to two errors after BCH-1 has corrected and established
the format bit. Short frames are passed to the text decoder as 112 bits instead
of being reported as long frames.

CNES orbitography frames remain 144-bit frames but are exempt from BCH-2
validation. T.001 reserves this protocol for LUT operators without describing
its payload, and the local corpus contains long orbitography frames whose
trailing field is not a valid BCH-2 codeword.

Validation covered all 121 previously accepted local FGB vectors, including
120 long orbitography frames, plus 19,266 injected one- and two-bit BCH-2
correction cases. Scanner builds and the mandatory synthetic SGB regression
also passed. Recovery of BCH-2 errors on a live non-orbitography FGB frame
remains to be observed.

## Version 10.2.10 - 2026-07-08 - README documentation cleanup

### English and French README cleanup

Added `README_FR.md` as the maintained French README and removed the obsolete
`README.fr.md` to avoid serving stale translated content.

Both README files were edited to keep README content focused on current
behavior rather than commit-history details:

- validation status now summarizes current field checks without naming internal
  fix branches or one-off short-window percentages;
- historical explanations about the former synchronous RTL-SDR path remain in
  this changelog instead of the README;
- SGB acquisition documentation now describes the actual staged search:
  ±8 kHz first, then ±16 kHz fallback only when needed;
- internal shorthand such as `flat-chain` was replaced by
  clearer README wording.

## Version 10.2.9 - 2026-07-04 - Wider SGB acquisition search

### SGB acquisition search widened with ±16 kHz fallback (`src/dsss_demod.c`)

`freq_acq_fft_corr()` is now tried first with the normal ±8 kHz
residual-frequency search window, then retried with a wider ±16 kHz fallback
when the first pass does not meet the acquisition confidence threshold. Field
dumps from firmin showed valid CNES SGB calibration bursts with correlation
peaks at roughly +8.6 to +11.1 kHz from the scanner burst centroid; a strict
±8 kHz-only search rejected those bursts before despreading even though the
signal was usable.

This is an acquisition-only change. The post-sync SGB path remains unchanged:
NCO wipeoff, OQPSK delay, despread, per-bit Costas, and BCH validation are the
same as in 10.2.8.

### Offline SGB EPL diagnostic tool (`utils/sgb_epl_diag.c`)

Added `build/sgb_epl_diag` to analyze dumped SGB bursts offline with
Early/Prompt/Late PRN correlators. The tool was used to separate true
structure loss from an acquisition search-window miss, and to verify that the
rejected firmin bursts still had strong prompt correlation outside ±8 kHz.

### Validation update

2026-07-04 local and firmin logs were rechecked on the 150 s CNES calibration
grid after the wider acquisition search:

- firmin: about 93 % SGB calibration-slot success in the checked post-fix
  window; recovered bursts had residual offsets outside the old ±8 kHz range;
- local RTL/Yagi: remained in the same high-success range, with no regression
  from the acquisition change;
- FGB: no regression observed; still in the pre-existing ~90 % field class;
- BCH/frame rejects: no return of the earlier "strong preamble, random data"
  failure mode.

The acquisition-only Butterworth filter remains available through
`ACQ_BANDPASS_HZ` for experiments, but the default is disabled (`0`).

## Version 10.2.8 - 2026-07-04 - Unified scanner, RTL async, cautious rate accounting

### Real-time scanner unified on `main`

`dec406_scan` now uses the unified scanner architecture with automatic backend
selection (Airspy Mini, RTL-SDR, PlutoSDR). The scanner reports the selected
gain and backend parameter (`ppm` for RTL-SDR) at startup.

### RTL-SDR asynchronous capture (`src/backend_rtlsdr.c`)

The RTL-SDR backend now uses `rtlsdr_read_async()` with 32 large buffers.
This replaces the synchronous capture loop, which could silently underfeed the
scanner and introduce sample discontinuities before the ring buffer. Local
RTL/Yagi validation showed the effective throughput returning to nominal
2.4576 MS/s and removed the failure mode where SGB had a strong preamble sync
but random payload bits.

`RTL_DIAG=1` enables periodic throughput logs; it is silent by default for
systemd operation.

### Fredzo SGB detection fixes integrated

Integrated the relevant fixes from fredzo's `bch2_correction` branch, adapted
to the unified scanner:

- reject degenerate all-zero/all-one SGB codewords before BCH acceptance;
- reject trivial BCH success after a false despread lock;
- cap `freq_acq_fft_corr()` lag search to the burst pre-roll region so late
  data/noise peaks cannot beat the true preamble.

### Rate accounting correction

Older changelog entries quote short-window headline rates such as "SGB 93 %"
and "FGB 92 %". Those numbers are **not stable global performance figures**:
they depend on propagation, time of day, local noise, active CNES beacons, and
scanner filtering. Current reporting should keep SGB buckets separate:

| Metric | Definition |
|---|---|
| SGB acquisition/sync | `(BCH OK + FRAME REJECTED) / detected SGB bursts` |
| SGB decoder purity | `BCH OK / (BCH OK + FRAME REJECTED)` |
| SGB end-to-end | `BCH OK / detected SGB bursts` |

Recent validation after this update:

- local RTL/Yagi: no return of the "strong preamble, random payload" failure;
  SGBs that synchronize validate BCH cleanly, while remaining misses are mostly
  `coarse reject conf ...` acquisition failures;
- firmin: early post-deploy sample is too short and propagation-dependent for
  a headline percentage. BCH rejects disappeared in the short post-deploy
  sample, but acquisition rejects remain the dominant SGB loss bucket.

## Version 10.2.7 - 2026-06-14 - SGB chain fixes (short-window rates obsolete)

### SGB decode chain fixes (`src/despread.c`, `src/dec406_v2g.c`, `src/dsss_demod.c`)

Three bugs fixed simultaneously. In the short validation window available at
the time this looked like a 29 % → 93 % improvement at firmin (80 km), but
that headline rate is now considered obsolete; see 10.2.8 for bucket-based
rate accounting.

1. **Bit polarity inversion** — T.018: data=1 inverts the PRN, so correlation
   with raw PRN is negative. Decision was `> 0` instead of `< 0`, causing all
   250 bits to be systematically inverted → BCH always failed.

2. **Preamble frequency estimation** — Linear regression on the 25 known
   preamble atan2 phases (unwrapped). Initialises `freq_per_bit` and
   `phase_rad` instead of relying on slow PLL convergence (alpha=0.04,
   beta=0.01). Measured residual: ~4 Hz (0.18 rad/bit).

3. **Chien search over full GF(2^8)** — BCH(250,202) shortened from
   BCH(255,207): search must cover 255 positions, not 250. Roots in the
   virtual padding (250-254) must be counted for `nroots == L` but don't
   flip bits.

### Multi-offset boxcar oracle (`src/dsss_demod.c`)

4 sub-chip boxcar offsets × 4 Costas phases = 16 combinations tried against
BCH. First combo that passes BCH wins. `bch_decode_250_202_nerr()` returns
error count for diagnostics.

### DUMP_FAIL burst capture (`src/main_scan.c`)

`DUMP_FAIL=1` environment variable dumps failed SGB bursts to
`burst_sgb_HHMMSS_<freq>Hz.cf32` (float32 complex) for offline analysis.

### Historical short-window decode rates at firmin relay (80 km, evening propagation)

These values are kept for historical context only. They should not be used as
current global performance claims.

| Type | Before | After |
|------|--------|-------|
| SGB  | 29 %   | 93 % (13/14, nerr=0 on all OK) |
| FGB  | 78 %   | 92 % (no FGB changes, propagation) |

---

## Version 10.2.6 - 2026-06-14 - FGB decode rate 78 %

### FGB IQ demodulator improvements (`src/fgb_iq_demod.c`)

**Dual-grid CW end detection** — `find_cw_end_cmplx()` now scans two
interleaved grids offset by `half/2`. The all-1s preamble in biphase-L
is uniquely vulnerable to half-bit misalignment: S1 and S2 each average
a full ±1.1 cycle → cancel to 0 → detector misses the 15-bit preamble
and triggers 17-18 bits late at FSYNC. Dual-grid eliminates 100 % of
these garbage bursts (was 29 % of captures).

**Multi-phase Costas search** — Try 4 initial phases {0°, 45°, 90°, 135°}
for each bit0 offset candidate (13 offsets × 4 phases = 52 candidates).
Selects best FSYNC score across all combinations.

**BCH1 brute-force error correction** — `bch1_correct()` flips 1, 2, or 3
bits in the BCH1 codeword (bits 24..105) and checks syndrome via
`test_crc1()`. BCH(82,61) t=3 per T.001 specification. Applied before
polarity fallback; polarity path also gets BCH correction.

**Diagnostic instrumentation** — `dump_costas_diag()` emits per-burst CSV
with cw_end, cw_mag, cw_expected, cw_thresh, and 4-phase preamble scores.
`dump_bits()` now includes soft values. Controlled by `FGB_IQ_DIAG` env var.

**Result at firmin relay (80 km)**: 45 % → 78 % FGB decode rate.

### Scanner tuning (`src/main_scan.c`)

- `BW_SPLIT_HZ` 50 kHz → 20 kHz: tighter FGB/SGB classification
- `CYCLE_SAMPLES` 55 s → 600 s: reduces cycle boundaries that truncate
  SGB bursts ('buffer too short') by a factor of 11

---

## Version 10.2.5 - 2026-06-01 - Production scanner wiring

Branch `feature/dsss-flat-chain`.

### DSSS chain refactor — flat-chain (commits `b6bbcc4`, `e5c5e5d`)

Removed the sample-rate tracking loop (FLL+PLL+DLL+Kalman) introduced in
10.2.4. The new `dsss_demod.c` chain is direct:
DC blocker → boxcar decimation to chip rate → `freq_acq_fft_corr` (FFT-corr)
→ NCO wipeoff → OQPSK delay → final boxcar → despread + per-bit Costas PLL.
Much less code, with equivalent OTA performance.

### FFT-correlation frequency acquisition (`freq_acq_fft_corr`)

~1 Hz precision using a pair of FFTs (signal × conj(PRN preamble)), sweeping
±8 kHz at 12 Hz spacing, then refining ±18 Hz at 1 Hz around the peak.
Metric `conf` = peak/mean over the grid, threshold `ACQ_CONF_MIN = 8.0`.

### Multi-rotation BCH oracle in `dsss_demod`

For each acquired burst, the 4 Costas phases (0°/90°/180°/270°) are tried
against `bch_decode_250_202`; the first decoding phase wins. This recovers
bursts where `despread_sync` selected the wrong phase at marginal SNR.
Cost is ~50 ms.

### FGB IQ-direct demodulator (`src/fgb_iq_demod.c`, commit `2d6cc73`)

1G complex-baseband decoder without the FM-demod → audio pipeline:
- Carrier-frequency estimation on the CW preamble (160 ms)
- CW end detection using smoothed |S1-S2| over two bits
- Bit-phase refinement by sweeping ±half_bit
- Multi-offset frame-sync search over 9 bits
- BPSK Costas loop + Manchester slicer
- CRC1/CRC2 validation

At the firmin relay (80 km from the test beacon): ~75 % decode rate,
equivalent to F4EHY (62 % reference on the same relay).

### Real-time scanner `dec406_scan`

Replaces the historical Perl pipeline `rtl_power + rtl_fm + sox +
dec406_audio`. Spectral burst detection over 100 kHz, FGB/SGB classification
by bandwidth, and synchronous `librtlsdr` ingestion.

**Synchronous librtlsdr capture** (`62e6e92`): `popen("rtl_sdr")` →
`rtlsdr_read_sync()`. Removed `cb transfer status: 5` during USB hiccups in
async mode. 55 s cycle driven by sample counter. Internal librtlsdr prints are
silenced with `dup2` around `rtlsdr_open/close`.

**Journal diagnostics by priority**: `DIAG`/`DWARN`/`DERR` macros
(`include/diag_log.h`) prefix stderr with kernel-syslog levels (`<7>`, `<4>`,
`<3>`). `journalctl -p info` gives clean F4EHY-style output; `-p debug` brings
back all `[fgb_iq]/[freq_acq]/[despread]/[dsss_demod]` diagnostics, heartbeat,
CRCs, Orbitography data, etc.

**T.012 email alerts** (`src/scan_alert.{c,h}`, ported from `scan406.pl`):
- Whitelist of T.012 Table H.2 distress channels (B/C/D/F/G/J/K/N/O/R/S,
  ±2 kHz); channel A (406.022 orbitography) excluded
- SMTP config in `data/config_mail.txt` (key=value, same format as Perl)
- Background `sendemail` to avoid blocking the pipeline during the Gmail TLS
  handshake (otherwise: ring overruns on SGB decodes)
- Body = header (UTC, type, freq, SNR, full hex frame) + decode block captured
  with `dup2(tmpfile)` around `decode_1g/decode_beacon`
- SGB guard: alerts only if T.018 §3 bit 43 = 0 (Normal Operation). Test
  transmissions (CNES on channel K) remain silent.

**Output readability**: millisecond timestamps, inter-burst `dt` computed in
samples (precise, immune to display jitter), and a blank line between decoded
frames.

### Small fixes

- `g_wr` / `overruns` reset on each rtl_sdr cycle (`f4649d5`)
- `rtl_sdr -n` for natural exit after 55 s (`f239e7c`, later replaced by
  synchronous librtlsdr reads)
- CW threshold lowered (0.1 → 0.08), sustain 3 → 4 (`9676768`)
- Removed "BCH could not correct" prints for rejected rotations (`33064cf`)
- Cleaned BCH error counting: only "N errors corrected" remains visible

### Synthetic regression

```
./build/dec406_iq ../../GNURADIO/test_sgb_halfsine.sigmf-data -s 2457600
```

→ z=9639.2, BCH validated on phase 0°, Hex ID decoded. OK.

---

## Version 10.2.4 - 2026-05-16 - Tracking loop + OTA decoding

Branch `feature/fll-pll-tracking`.

### OTA demodulator working

The over-the-air signal (PlutoSDR → RTL-SDR / SDRangel) now decodes with clean
BCH. The receive chain is replaced by a sample-rate **tracking loop**
(FLL+PLL+DLL+Kalman) that performs carrier tracking and decimation in a single
pass, replacing the RRC filter + QPSK Costas chain.

### Receive chain (`src/dsss_demod.c`)

1. DC blocker (IIR α=0.001) on raw samples
2. `freq_acq_coarse_fft()` — 4th-power FFT, ±25 kHz plausibility check
3. ±300 Hz sweep fallback (PRN correlation) if FFT is rejected
4. OQPSK delay (Q advanced by SPS/2)
5. Tracking loop → chip-rate output
6. `despread_burst()` — preamble sync + bit extraction

### Fixes

#### Decode bursts anywhere in the window (commit 8ffb090)

- `DESPREAD_SYNC_RANGE` 1000 → 9600 chips (= one full scan step)
- Scan window 1.1 s → 1.35 s
- **Cause**: a burst whose preamble fell beyond offset 1000 was never found,
  causing lottery-style decoding on short files

#### Reject false beacons (commit fa08382)

- `DESPREAD_SYNC_THRESHOLD` 2.8 → 20: noise (z ≤ 7) no longer synchronizes
- `bch_decode_250_202()` now returns a status; `decode_2g()` rejects the frame
  and prints no beacon if BCH cannot correct
- **Cause**: on weak links, the decoder synchronized on noise and printed a
  fabricated beacon (false TAC, false GPS position)

#### Phase tracking in despread (commits 08d8a0a, ad3cc7e, 0995092)

- 2nd-order BPSK phase tracker (proportional + integral) in `despread_bits()`
- PLL: phase correction replaced by a one-shot frequency offset (no phase jump
  at epoch boundaries)

#### Complex-correlation preamble sync (commit 16d00d6)

- Complex correlation `|Σ s·conj(e)|` insensitive to carrier phase
- Costas ambiguity resolution over 4 phases

### New components

- `src/tracking.c` — sample-rate tracking loop (EPL, 3-state ATC, P² lock)
- `src/kalman5.c` — 5-state Kalman filter (optional, disabled)
- `src/freq_acq.c` — coarse frequency acquisition (4th-power FFT + sweep)
- `tests/test_bch_reject.c` — clean / corrected / rejected BCH path tests

---

## Version 10.2.3 - 2025-10-24 - IQ demodulator investigation

### Structural misalignment investigation

- **Added full debug traces**: checked index alignment at every step
  (AGC → Despreading)
- **Major finding**: no structural misalignment detected
- **Identified root cause**: test files truncated by 1 sample (float32 rounding
  errors)

### Major fixes

#### Preamble search window (commit fc6f617)

```c
// Extend 20% -> 50% for better detection
size_t search_length = num_samples / 2;  // Was: num_samples / 5
```

**Results:**
- Preamble index: 350,870 → 0
- Recovered symbols: 33,062 → 38,399 (99.997%)
- Phase correlation: 63% → 88%

#### Manual sample-rate option (commit e458450)

- Bypasses auto-detection (8 min → 0.87 sec)
- New `-s <rate>` option to specify sample rate manually

### New tools

#### test_sample_rate (commit 1e3a624)

- Sample-rate detection by preamble correlation
- Tests 10 common sample rates (300 kHz → 6.144 MHz)
- DSSS correlation for precise estimation

```bash
./test_sample_rate file.iq
# Output: Estimated sample rate with correlation score
```

#### resample_iq

- IQ resampling with libsamplerate
- Conversion between sample rates (e.g. 2.5 MHz → 384 kHz)
- High-quality interpolation (SRC_SINC_BEST_QUALITY)

### Test files

**Discovered issue:**
- `test_known_384kHz.iq`: 383,999 samples (missing 1)
- `test_known.iq` @ 2.5MHz: 2,499,999 samples (missing 1)

**Impact:** despreading correlation 5% instead of expected >70%

**Generator fix** (SARSAT_SGB commit 1d4493f):
- Fixed float32 rounding error in OQPSK modulator
- Complete files now generated: 2,500,000 samples

### Technical analysis

#### Timing recovery

- Loop condition adjusted (cosmetic, no impact)
- Recovered 38,399/38,400 symbols (99.997%)
- Limited by truncated test files, not by the algorithm

#### Phase ambiguity resolution

- Extended search: 360° × 2 swaps × 2 inversions
- Phase 1 correlation: 88% (excellent improvement)
- Phase 2 (chip offset): extended search -15 to +15

### Documentation

- **BILAN_SESSION_20251024.md**: complete misalignment investigation
- Debug traces kept for future diagnostics
- Truncated-file analysis documented

### Validation

**Demodulator validated as functional**
- Processing chain is correct (no structural bug)
- Low correlation (5%) caused only by test files
- Expected >70% with complete files and signal at file start

### Next steps

1. Generate test file with signal at the beginning of the file
2. Validate correlation >70% with a correct file
3. Document complete TX → RX workflow

---

## Version 10.2.2 - 2025-10-19 - 2G demodulator paused

### Status: PAUSED

- Demodulator not functional (55.3% bit accuracy)
- Full documentation in ETAT_PAUSE_DEMODULATEUR.md
- 4 identified bugs (timing, phase, DSSS, Costas)

---

## Version 10.2.1 - 2025-09-04 - Complete T.001 implementation

### Major features added

- **Ship Security Protocol (Protocol 12)**: complete decoding with `[SECURITY]`
  marker
- **Standard Test Protocol (Protocol 14)**: hexadecimal test-data decoding
- **National Test Protocol (Protocol 15)**: national-use data decoding
- **Radio Call Sign User Protocol (Protocol 6)**: 7-character Baudot decoding
- **Test User Protocol (Protocol 7)**: improved test-user decoding

### Existing protocol improvements

- **Orbitography Protocol (Protocol 0)**:
  - Specialized orbitography-data decoding (5 bytes + 6 bits)
  - Fixed identification of 406.022 MHz calibration beacons
- **National User Protocol (Protocol 4)**: complete national-data extraction
- **Aviation/Maritime User Protocols**: improved Baudot decoding for call signs

### Technical functions added

```c
// New specialized decoding functions
decode_orbitography_data()      // Calibration/orbitography beacons
decode_standard_test_data()     // Standard test protocol
decode_test_beacon_data()       // Test beacon data
decode_national_use_data()      // National-use data
decode_radio_callsign_data()    // Radio call signs
decode_baudot_char()            // Full Baudot characters
display_baudot_42()             // 6-character Aviation display
display_baudot_2()              // Extended 7-character display
```

### Compliance impact

- **Before**: T.001 95% implemented + T.018 implemented = 95% implemented
- **After**: T.001 100% implemented + T.018 implemented = 100% implemented

**Validated tests**: Orbitography Protocol (406.022 MHz calibration beacons)
and Test User Protocol only.
**Limitation**: other protocols implemented according to specifications but
not tested on real beacons.

### Protocols now supported (complete)

#### Location Protocols (P=0)

- [x] Protocol 2: EPIRB MMSI
- [x] Protocol 3: ELT 24-bit
- [x] Protocol 4: ELT serial
- [x] Protocol 5: ELT operator
- [x] Protocol 6: EPIRB serial
- [x] Protocol 7: PLB serial
- [x] Protocol 8: National ELT
- [x] Protocol 9: ELT(DT)
- [x] Protocol 10: National EPIRB
- [x] Protocol 11: National PLB
- [x] **Protocol 12: Ship Security** (new)
- [x] Protocol 13: RLS Location
- [x] **Protocol 14: Standard Test** (new)
- [x] **Protocol 15: National Test** (new)

#### User Protocols (P=1)

- [x] **Protocol 0: Orbitography** (improved)
- [x] Protocol 1: ELT Aviation User
- [x] Protocol 2: EPIRB Maritime User
- [x] Protocol 3: Serial User
- [x] **Protocol 4: National User** (improved)
- [x] **Protocol 6: Radio Call Sign** (new)
- [x] **Protocol 7: Test User** (improved)

### Validated tests

- Builds without errors/warnings
- 406.022 MHz calibration beacon test (Orbitography Protocol - correct
  identification)
- User test beacon test (Test User Protocol - functional decoding)
- New protocols (Ship Security, Standard Test, National Test, Radio Call Sign):
  implemented according to specifications, not tested
- Regression: existing protocols preserved

### Standard references

- **COSPAS-SARSAT T.001**: 100% implemented (partial tests: orbitography,
  test user)
- **COSPAS-SARSAT T.018**: complete implementation (not tested on real beacons)
- **ITU-R M.585**: MID database complete
- **Modified Baudot**: complete implementation (partially tested)

---

## Version 10.2.0 - 2025-08-xx

### Initial features

- Complete 1G decoder (T.001 95% compliance)
- Complete 2G decoder (T.018 100% compliance)
- Real-time audio support
- Complete MID database
- Email automation scripts
- OpenStreetMap geolocation

### Architecture

- Complete modularity (5 main modules)
- Optimized audio pipeline
- BCH(250,202) error correction
- Multi-format support (hex, WAV, real time)
