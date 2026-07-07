# dec406 — COSPAS-SARSAT 406 MHz Beacon Decoder

Decoder for 1st (FGB) and 2nd (SGB) generation COSPAS-SARSAT emergency
beacons at 406 MHz.

**Branch**: `main`

---

## What works

### Decoders (bit-perfect)
- **1G (FGB)**: T.001 biphase-L PSK ±1.1 rad, 400 bps, all Location/User
  protocols — `dec406_v1g.c`
- **2G (SGB)**: BCH(250,202) with full Berlekamp-Massey + Chien error
  correction, all T.018 fields — `dec406_v2g.c`

### Demodulators
- **DSSS OQPSK (2G)** — `dsss_demod.c` flat-chain (no sample-rate tracking
  loop): DC blocker → FFT-correlation frequency acquisition → NCO wipeoff →
  OQPSK delay → multi-offset boxcar decimation → despread with preamble
  linear-fit frequency estimation + per-bit Costas PLL → BCH. Oracle tries
  4 boxcar offsets × 4 Costas phases (16 combos) before giving up. The
  acquisition lag search is capped to the burst pre-roll to avoid late
  noise/data peaks beating the true preamble. The FFT-correlation frequency
  search spans ±16 kHz, which covers scanner centroid errors observed on
  real CNES SGB bursts.
- **FGB IQ-direct (1G)** — `fgb_iq_demod.c`: complex baseband BPSK biphase-L
  decoder without FM-demod → audio detour. Dual-grid CW end detection,
  multi-phase Costas search (4 initial phases × 13 offsets), Manchester
  slicer, BCH1 brute-force error correction (t=3), CRC.

### Real-time scanner
`dec406_scan` is a unified real-time scanner with automatic SDR backend
selection for interactive use (Airspy Mini, RTL-SDR, PlutoSDR, HackRF).
The RTL-SDR backend uses `rtlsdr_read_async()` with large buffers; the
previous synchronous path could silently underfeed the scanner and corrupt
long FGB/SGB bursts. The scanner runs a spectral burst detector over the
100 kHz band, classifies each burst as FGB or SGB by bandwidth, and decodes
accordingly. For services, the backend can be forced explicitly with
`DEC406_BACKEND=rtl|airspy|pluto|hackrf` so there is no startup probing.

### Current validation status

Rates depend strongly on propagation, time of day, local noise, and which CNES
system beacons are active. Track three SGB buckets separately:

| Metric | Definition |
|--------|------------|
| SGB acquisition/sync | `(BCH OK + FRAME REJECTED) / detected SGB bursts` |
| SGB decoder purity | `BCH OK / (BCH OK + FRAME REJECTED)` |
| SGB end-to-end | `BCH OK / detected SGB bursts` |

Recent validation after the async RTL fix, fredzo SGB corrections, and the
±16 kHz acquisition search showed no "strong preamble sync then random data"
failures: SGBs that synchronize validate BCH cleanly. On 2026-07-04 the firmin
relay recovered calibration SGB bursts whose residual offsets were around
+8.6 to +11.1 kHz, outside the previous ±8 kHz search window; the same run
showed roughly 93 % SGB calibration-slot success on the 150 s grid. Local
RTL/Yagi validation remained in the same range, and FGB stayed around its
pre-existing 90 % class. Treat these as run-specific field checks, not fixed
global rates.

---

## Build

```bash
make build/dec406_iq        # SGB decoder from IQ file
make build/dec406_scan      # Real-time scanner (needs librtlsdr-dev)
make                        # all targets
```

Binaries produced in `build/`:

| Binary | Purpose |
|--------|---------|
| `dec406_iq` | SGB DSSS/OQPSK demodulator from IQ file |
| `dec406_hex` | 1G/2G decoder from hex string |
| `dec406_audio` | 1G decoder from WAV file (legacy FM-demod pipeline) |
| `dec406_scan` | Real-time FGB+SGB band scanner (Airspy/RTL-SDR/PlutoSDR) |
| `dec406_dsss_test` | DSSS demodulator unit test driver |
| `generate_2g_hex` | 2G test frame generator |
| `reset_usb` | USB device reset utility |

---

## Usage

### Offline IQ files

```bash
# SGB from IQ file (float32 complex, default)
./build/dec406_iq signal.iq -s 2457600

# SDRangel ci32_le recording (int32 complex little-endian)
./build/dec406_iq recording.sigmf-data -s 2457600 -I

# RTL-SDR uint8 recording
./build/dec406_iq recording.iq -s 2457600 -u
```

Input formats: float32 complex (default), `-u` RTL-SDR uint8, `-i` int16
interleaved, `-I` int32 SDRangel ci32_le.

### Hex frames

```bash
./build/dec406_hex 09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4
./build/dec406_hex 8E3301E2402B002BBA863609670908
```

### Real-time scanner

```bash
./build/dec406_scan 406.0M 406.1M             # AGC, ppm=0
./build/dec406_scan 406.0M 406.1M 0 30        # ppm=0, fixed gain 30 dB
```

The scanner reads samples at 2.4576 Msps, detects bursts on a power
spectrogram, classifies them by bandwidth (`BW_SPLIT_HZ = 20 kHz`; wider
bursts are SGB), and runs the appropriate decoder. On RTL-SDR it resets the
USB device at startup, then captures continuously through the asynchronous
librtlsdr API.

Set `RTL_DIAG=1` to log effective RTL throughput every few seconds:

```bash
RTL_DIAG=1 ./build/dec406_scan 406.0M 406.1M
```

Useful SGB diagnostics:

```bash
DSSS_DIAG=1 ./build/dec406_scan 406.0M 406.1M
DUMP_FAIL=1 ./build/dec406_scan 406.0M 406.1M
make build/sgb_epl_diag
```

`DUMP_FAIL=1` writes failed SGB burst windows as `burst_sgb_*.cf32` for
offline replay. `build/sgb_epl_diag` probes those dumps with EPL
correlators at wider residual-frequency ranges. `ACQ_BANDPASS_HZ` is kept as
a diagnostic acquisition-only filter; default `0` leaves it disabled.

#### 1544 MHz downlink service

`systemd/scan1544.service` is the dedicated downlink unit. It keeps the
backend explicit and switches the alert path to geographic filtering instead
of the 406 MHz distress-channel whitelist:

```ini
Environment=DEC406_BACKEND=hackrf
Environment=DEC406_ALERT_MODE=downlink
Environment=DEC406_ALERT_CENTER=43.0,1.5
Environment=DEC406_ALERT_RADIUS_KM=80
```

If Pluto is used instead of HackRF on a given station, only
`DEC406_BACKEND` changes. In downlink mode, an email is sent only when the
decoded beacon position is valid and inside the configured radius.

#### As a systemd service

```ini
[Unit]
Description=Scan406 Service
After=network.target

[Service]
Type=simple
SyslogIdentifier=scan406
WorkingDirectory=/home/firmin/balise_406MHz/dec406_scan/
ExecStart=/home/firmin/balise_406MHz/dec406_scan/build/dec406_scan 406.0M 406.1M
Restart=always
RestartSec=25
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
```

Stderr is tagged with systemd journal priority prefixes:

```bash
journalctl -u scan406 -p info      # clean trace (decoded frames only)
journalctl -u scan406 -p debug     # full diagnostic stream
```

#### Email alerts on T.012 distress channels

If a `data/config_mail.txt` is present, the scanner emails the decoded
frame whenever a beacon decodes on a T.012 Table H.2 distress channel
(B/C/D/F/G/J/K/N/O/R/S, ±2 kHz). The `sendemail` binary must be installed.

At startup the banner prints the mail settings that matter for operation:

```text
alerts  : enabled
mail smtp : smtp.gmail.com:587
mail user : xxxx@gmail.com
mail to   : a@b.fr,c@d.fr
```

To enable alerts, copy the template and fill in the four values:

```bash
cp data/config_mail.txt.example data/config_mail.txt
$EDITOR data/config_mail.txt          # set your SMTP creds + recipients
chmod 600 data/config_mail.txt        # contains an app password
```

Format (key=value, one per line):

```
smtp_serveur=smtp.gmail.com:587
utilisateur=user@example.org
password=app_password
destinataires=a@b.com,c@d.fr
```

Without that file the scanner runs normally; the banner prints
`alerts : disabled` and no email is sent.

Channel A (406.022 — orbitography/calibration) is excluded by design.
Additional silencing filters layered on the channel whitelist:
- FGB beacons identified as Orbitography or with `ID-NOT-AVAIL`
  (factory-fresh / bench tests) never trigger.
- SGB test transmissions (T.018 §3 bit 43 = 1) never trigger.
- A second sighting of the same Hex ID within 3 min is required
  before mailing (filters one-shot bench-test bursts; a real distress
  beacon repeats every ~50 s and is confirmed at the second burst).

---

## 2G Demodulation Chain

```
IQ @ 2.4576 MHz
  → DC blocker (IIR α=0.001)
  → boxcar decimation to chip rate (acquisition only)
  → freq_acq_fft_corr (chip-rate FFT-correlation, ±16 kHz, ~1 Hz precision)
  → NCO wipeoff at sample rate
  → OQPSK delay (Q advanced by SPS/2)
  → multi-offset boxcar decimation (4 sub-chip offsets)
  → despread (preamble linear-fit freq/phase estimation,
       per-bit BPSK Costas PLL over 256 chips)
  → 250 bits → bch_decode_250_202 (nerr) → decode_beacon
```

For each acquired burst the chain tries 4 boxcar offsets × 4 Costas phases
(16 combos) against BCH, keeping the first that decodes cleanly. Preamble
linear regression initialises carrier frequency/phase, replacing slow PLL
convergence. A frame whose codeword BCH cannot correct is rejected.

---

## 1G Demodulation Chain (IQ-direct)

```
IQ
  → multi-stage moving-average decimation to ~9.6 kHz
  → CW preamble carrier-frequency estimate (sample-level phase diffs)
  → frequency wipeoff
  → dual-grid CW end detection (two grids offset half/2)
  → multi-phase Costas search (4 phases × 13 offsets)
  → Manchester slicer (biphase-L ±1.1 rad)
  → BCH1 brute-force correction (t=3, bits 24..105)
  → 144 bits → CRC1/CRC2 validation + polarity fallback
```

No FM-demod, no audio detour, no biphase-L codec dependency.

---

## Project Structure

```
src/         C sources (dsss_demod, despread, freq_acq, fgb_iq_demod,
              dec406_v1g, dec406_v2g, main_iq, main_scan, scan_alert, ...)
include/     Headers
build/       Compiled binaries
tests/       SGB codec unit tests, BCH reject tests
utils/       Offline diagnostic tools
scripts/     Analysis / debug scripts
docs/        Specifications, deployment notes, SGB status notes
data/        Runtime data (config_mail.txt, etc.)
```

---

## Documentation

- `CHANGELOG.md` — Version history
- `CLAUDE.md` — Project conventions, debug methodology
- `docs/ARCHITECTURE_dec406.md` — Detailed architecture (French)
- `docs/TESTS_VALIDATION.md` — Validation procedures
- `scripts/scan406.pl` — Legacy Perl scanner (superseded by dec406_scan)

---

## License

This project is licensed under the MIT License. See `LICENSE`.

---

## References

- **C/S T.001** — 1st Generation Beacon Specification (FGB)
- **C/S T.012** — Beacon Type Approval (channel list, Table H.2)
- **C/S T.018** — 2nd Generation Beacon Specification (SGB)
- `gr-cospas` / `gr-satellites` — GNU Radio receivers
- `sgb-codec` (jbirby) — Python reference SGB codec
- `sarsat_sgb` (ADALM-PLUTO) — SGB modulator for PlutoSDR
- `scan406.pl` (F4EHY 2020) — Perl scanner ancestor, ported to C in
  `dec406_scan` + `scan_alert`
