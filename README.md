# dec406 — COSPAS-SARSAT 406 MHz Beacon Decoder

Decoder for 1st (FGB) and 2nd (SGB) generation COSPAS-SARSAT emergency
beacons at 406 MHz.

**Branch**: `feature/dsss-flat-chain`

---

## What works

### Decoders (bit-perfect)
- **1G (FGB)**: T.001 biphase-L PSK ±1.1 rad, 400 bps, all Location/User
  protocols — `dec406_v1g.c`
- **2G (SGB)**: BCH(250,202) with full Berlekamp-Massey + Chien error
  correction, all T.018 fields — `dec406_v2g.c`

### Demodulators
- **DSSS OQPSK (2G)** — `dsss_demod.c` flat-chain (no sample-rate tracking
  loop): DC blocker → boxcar decimation to chip rate → FFT-correlation
  frequency acquisition (`freq_acq_fft_corr`) → NCO wipeoff → OQPSK delay →
  despread with per-bit Costas PLL → BCH. Multi-rotation BCH oracle tries
  the 4 Costas phases before giving up.
- **FGB IQ-direct (1G)** — `fgb_iq_demod.c`: complex baseband BPSK biphase-L
  decoder without FM-demod → audio detour. CW preamble detection, frame
  sync, Manchester slicer, multi-offset FSYNC sweep, Costas loop, CRC.

### Real-time scanner
`dec406_scan` ingests RTL-SDR samples directly via librtlsdr (synchronous
mode), runs a spectral burst detector over the 100 kHz band, classifies
each burst as FGB or SGB by bandwidth, and decodes accordingly. Designed
to run as a systemd service; see "Real-time scanner" below.

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
| `dec406_scan` | Real-time FGB+SGB band scanner (rtl-sdr) |
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
spectrogram, classifies them by bandwidth (≥ 40 kHz → SGB), and runs the
appropriate decoder. Each cycle is 55 s; the dongle is then closed,
USB-reset, and reopened to clear accumulated libusb state.

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
  → boxcar decimation to chip rate (un-delayed, acquisition only)
  → freq_acq_fft_corr (chip-rate FFT-correlation, ~1 Hz precision)
  → NCO wipeoff at sample rate
  → OQPSK delay (Q advanced by SPS/2)
  → boxcar decimation to chip rate (final chip stream)
  → despread (2-pass preamble sync, complex 4-phase Costas resolution,
       per-bit BPSK Costas PLL over 256 chips)
  → 250 bits → bch_decode_250_202 → decode_beacon
```

For each acquired burst the chain tries all 4 Costas phases against BCH,
keeping the one that decodes cleanly. A frame whose codeword BCH cannot
correct is rejected — no beacon is printed from noise.

---

## 1G Demodulation Chain (IQ-direct)

```
IQ
  → multi-stage moving-average decimation to ~9.6 kHz
  → CW preamble carrier-frequency estimate (sample-level phase diffs)
  → frequency wipeoff
  → CW end detection (smoothed |S1-S2| crossing)
  → bit-period phase refinement
  → multi-offset FSYNC sweep
  → Costas BPSK loop + Manchester slicer
  → 144 bits → CRC1/CRC2 validation
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
scripts/     Analysis / debug scripts
docs/        T.018 specifications, architecture (not on GitHub)
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

## References

- **C/S T.001** — 1st Generation Beacon Specification (FGB)
- **C/S T.012** — Beacon Type Approval (channel list, Table H.2)
- **C/S T.018** — 2nd Generation Beacon Specification (SGB)
- `gr-cospas` / `gr-satellites` — GNU Radio receivers
- `sgb-codec` (jbirby) — Python reference SGB codec
- `sarsat_sgb` (ADALM-PLUTO) — SGB modulator for PlutoSDR
- `scan406.pl` (F4EHY 2020) — Perl scanner ancestor, ported to C in
  `dec406_scan` + `scan_alert`
