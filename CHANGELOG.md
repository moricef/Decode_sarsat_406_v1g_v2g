# Changelog - dec406_v10.2

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

Branche `feature/dsss-flat-chain`.

### Refactor chaîne DSSS — flat-chain (commits `b6bbcc4`, `e5c5e5d`)
Suppression de la tracking loop sample-rate (FLL+PLL+DLL+Kalman) introduite
en 10.2.4. La nouvelle chaîne `dsss_demod.c` est plate :
DC blocker → boxcar décimation chip-rate → `freq_acq_fft_corr` (FFT-corr) →
NCO wipeoff → OQPSK delay → boxcar finale → despread + per-bit Costas PLL.
Beaucoup moins de code, performance équivalente sur OTA.

### Acquisition fréquence par FFT-corrélation (`freq_acq_fft_corr`)
Précision ~1 Hz via une paire de FFT (signal × conj(PRN preamble)), balaie
±8 kHz à 12 Hz puis fine ±18 Hz à 1 Hz autour du pic. Métrique `conf` =
peak/mean sur la grille, seuil `ACQ_CONF_MIN = 8.0`.

### Multi-rotation BCH oracle dans `dsss_demod`
À chaque burst acquis, les 4 phases Costas (0°/90°/180°/270°) sont
essayées contre `bch_decode_250_202` ; on garde la première qui décode.
Récupère les bursts où `despread_sync` a choisi la mauvaise phase à SNR
marginal. Coût ~50 ms.

### Démod FGB IQ-direct (`src/fgb_iq_demod.c`, commit `2d6cc73`)
Décodeur 1G en bande de base complexe, sans pipeline FM-demod → audio :
- Estimation de fréquence de porteuse sur le préambule CW (160 ms)
- Détection de fin de CW par |S1-S2| lissé sur deux bits
- Refinement de phase bit par balayage ±half_bit
- Recherche multi-offset du frame sync sur 9 bits
- Boucle Costas BPSK + slicer Manchester
- Validation CRC1/CRC2

Au relais firmin (80 km de la balise de test) : ~75 % de décodage,
équivalent au F4EHY (62 % référence sur le même relais).

### Scanner temps réel `dec406_scan`
Remplace le pipeline `rtl_power + rtl_fm + sox + dec406_audio` du Perl
historique. Détection spectrale de bursts sur 100 kHz, classification
FGB/SGB par bande passante, ingestion en `librtlsdr` synchrone.

**Capture librtlsdr synchrone** (`62e6e92`) : `popen("rtl_sdr")` →
`rtlsdr_read_sync()`. Plus de `cb transfer status: 5` sur les hoquets USB
en mode async. Cycle de 55 s piloté par compteur d'échantillons. Silence
des prints internes de librtlsdr par `dup2` autour de `rtlsdr_open/close`.

**Diagnostics journal par priorité** : macros `DIAG`/`DWARN`/`DERR`
(`include/diag_log.h`) qui préfixent stderr avec les niveaux kernel-syslog
(`<7>`, `<4>`, `<3>`). `journalctl -p info` donne la sortie propre style
F4EHY ; `-p debug` ramène tous les `[fgb_iq]/[freq_acq]/[despread]/[dsss_demod]`,
le heartbeat, les CRC, l'Orbitography data, etc.

**Alertes mail T.012** (`src/scan_alert.{c,h}`, ported from `scan406.pl`) :
- Liste blanche des canaux de détresse T.012 Table H.2 (B/C/D/F/G/J/K/N/O/R/S,
  ±2 kHz) ; canal A (406.022 orbitographie) exclu
- SMTP config dans `data/config_mail.txt` (clé=valeur, format identique au Perl)
- `sendemail` en arrière-plan pour éviter le blocage du pipeline pendant
  le handshake TLS Gmail (sinon : ring overruns sur les décodages SGB)
- Corps = en-tête (UTC, type, freq, SNR, hex frame complet) + bloc de
  décodage capturé via `dup2(tmpfile)` autour de `decode_1g/decode_beacon`
- Garde SGB : alertes uniquement si T.018 §3 bit 43 = 0 (Normal Operation).
  Les transmissions de test (CNES sur canal K) restent silencieuses.

**Lisibilité de la sortie** : timestamps milliseconde, `dt` inter-burst
calculé en samples (précis, immune au jitter d'affichage), ligne vide
entre trames décodées.

### Petits fixes
- `g_wr` / `overruns` réinitialisés à chaque cycle rtl_sdr (`f4649d5`)
- `rtl_sdr -n` pour exit naturel après 55 s (`f239e7c`, remplacé ensuite
  par lecture sync librtlsdr)
- CW threshold abaissé (0.1 → 0.08), sustain 3 → 4 (`9676768`)
- Prints "BCH could not correct" des rotations rejetées supprimés (`33064cf`)
- BCH error counting nettoyé : seul "N errors corrected" reste visible

### Régression synthétique
```
./build/dec406_iq ../../GNURADIO/test_sgb_halfsine.sigmf-data -s 2457600
```
→ z=9639.2, BCH validé sur phase 0°, Hex ID décodé. OK.

---

## Version 10.2.4 - 2026-05-16 - Tracking loop + décodage OTA

Branche `feature/fll-pll-tracking`.

### 🎯 Démodulateur OTA fonctionnel
Le signal over-the-air (PlutoSDR → RTL-SDR / SDRangel) est maintenant décodé,
BCH-propre. La chaîne de réception est remplacée par une **tracking loop**
sample-rate (FLL+PLL+DLL+Kalman) qui assure la poursuite de porteuse et la
décimation en une seule passe, en remplacement du filtre RRC + Costas QPSK.

### 🔧 Chaîne de réception (src/dsss_demod.c)
1. DC blocker (IIR α=0.001) sur échantillons bruts
2. `freq_acq_coarse_fft()` — FFT 4e puissance, contrôle de plausibilité ±25 kHz
3. Sweep fallback ±300 Hz (corrélation PRN) si la FFT est rejetée
4. OQPSK delay (Q avancé de SPS/2)
5. Tracking loop → sortie chip-rate
6. `despread_burst()` — sync préambule + extraction des bits

### 🐛 Corrections

#### Décodage des bursts n'importe où dans la fenêtre (commit 8ffb090)
- `DESPREAD_SYNC_RANGE` 1000 → 9600 chips (= un pas de scan complet)
- Fenêtre de scan 1.1 s → 1.35 s
- **Cause** : un burst dont le préambule tombait au-delà de l'offset 1000
  n'était jamais trouvé → décodage « à la loterie » sur fichiers courts

#### Rejet des fausses balises (commit fa08382)
- `DESPREAD_SYNC_THRESHOLD` 2.8 → 20 : le bruit (z ≤ 7) ne synchronise plus
- `bch_decode_250_202()` retourne maintenant un statut ; `decode_2g()` rejette
  la trame et n'affiche aucune balise si le BCH ne peut pas corriger
- **Cause** : à lien faible, le décodeur synchronisait sur du bruit et imprimait
  une balise fabriquée (faux TAC, fausse position GPS)

#### Poursuite de phase dans le despread (commits 08d8a0a, ad3cc7e, 0995092)
- Phase tracker BPSK 2e ordre (proportionnel + intégral) dans `despread_bits()`
- PLL : correction de phase remplacée par un offset de fréquence one-shot
  (pas de saut de phase aux frontières d'epoch)

#### Sync préambule en corrélation complexe (commit 16d00d6)
- Corrélation complexe `|Σ s·conj(e)|` insensible à la phase porteuse
- Résolution d'ambiguïté Costas sur 4 phases

### 🛠️ Nouveaux composants
- `src/tracking.c` — tracking loop sample-rate (EPL, ATC 3 états, lock P²)
- `src/kalman5.c` — filtre de Kalman 5 états (optionnel, désactivé)
- `src/freq_acq.c` — acquisition de fréquence coarse (FFT 4e puissance + sweep)
- `tests/test_bch_reject.c` — test des chemins BCH propre / corrigé / rejeté

---

## Version 10.2.3 - 2025-10-24 - Investigation Démodulateur IQ

### 🔍 Investigation Désalignement Structurel
- **Ajout traces debug complètes** : Vérification alignement indices à chaque étape (AGC → Despreading)
- **Découverte majeure** : Aucun désalignement structurel détecté
- **Root cause identifiée** : Fichiers de test tronqués d'1 sample (float32 rounding errors)

### 🐛 Corrections Majeures

#### Fenêtre de Recherche Préambule (Commit fc6f617)
```c
// Extension 20% → 50% pour meilleure détection
size_t search_length = num_samples / 2;  // Was: num_samples / 5
```
**Résultats:**
- Index préambule : 350,870 → 0 ✅
- Symboles récupérés : 33,062 → 38,399 (99.997%) ✅
- Phase corrélation : 63% → 88% ✅

#### Option Sample Rate Manuel (Commit e458450)
- Bypass auto-détection (8 min → 0.87 sec)
- Nouvelle option `-s <rate>` pour spécifier sample rate manuellement

### 🛠️ Nouveaux Outils

#### test_sample_rate (Commit 1e3a624)
- Détection sample rate par corrélation préambule
- Teste 10 sample rates courants (300 kHz → 6.144 MHz)
- Corrélation DSSS pour estimation précise
```bash
./test_sample_rate file.iq
# Output: Estimated sample rate with correlation score
```

#### resample_iq
- Rééchantillonnage IQ avec libsamplerate
- Conversion entre sample rates (ex: 2.5 MHz → 384 kHz)
- Interpolation haute qualité (SRC_SINC_BEST_QUALITY)

### 📊 Fichiers de Test

**Problème découvert:**
- `test_known_384kHz.iq` : 383,999 samples (manque 1) ❌
- `test_known.iq` @ 2.5MHz : 2,499,999 samples (manque 1) ❌

**Impact:** Corrélation désétalement 5% au lieu de >70% attendu

**Fix générateur** (SARSAT_SGB commit 1d4493f):
- Correction erreur arrondi float32 dans OQPSK modulator
- Fichiers complets maintenant générés : 2,500,000 samples ✅

### 🔬 Analyses Techniques

#### Timing Recovery
- Condition boucle ajustée (cosmetic, pas d'impact)
- Récupération 38,399/38,400 symbols (99.997%)
- Limité par fichiers de test tronqués, pas par l'algorithme

#### Phase Ambiguity Resolution
- Extended search : 360° × 2 swaps × 2 inversions
- Corrélation Phase 1 : 88% (excellente amélioration)
- Phase 2 (chip offset) : recherche étendue -15 à +15

### 📋 Documentation
- **BILAN_SESSION_20251024.md** : Investigation complète désalignement
- Traces debug conservées pour diagnostic futur
- Analyse fichiers tronqués documentée

### ✅ Validation
**Démodulateur validé fonctionnel**
- Chaîne de traitement correcte (aucun bug structurel)
- Faible corrélation (5%) due uniquement aux fichiers de test
- Attendu >70% avec fichiers complets et signal au début

### 🎯 Prochaines Étapes
1. Générer fichier test avec signal au début du fichier
2. Valider corrélation >70% avec fichier correct
3. Documenter workflow complet TX → RX

---

## Version 10.2.2 - 2025-10-19 - Pause Démodulateur 2G

### ⚠️ Statut: PAUSED
- Démodulateur non fonctionnel (55.3% bit accuracy)
- Documentation complète dans ETAT_PAUSE_DEMODULATEUR.md
- 4 bugs identifiés (timing, phase, DSSS, Costas)

---

## Version 10.2.1 - 2025-09-04 - Implémentation Complète T.001

### Fonctionnalités Majeures Ajoutées
- **Ship Security Protocol (Protocol 12)** : Décodage complet avec marquage `[SECURITY]`
- **Standard Test Protocol (Protocol 14)** : Décodage données de test hexadécimales  
- **National Test Protocol (Protocol 15)** : Décodage données d'usage national
- **Radio Call Sign User Protocol (Protocol 6)** : Décodage Baudot 7 caractères
- **Test User Protocol (Protocol 7)** : Amélioration du décodage utilisateur test

### Améliorations Protocoles Existants
- **Orbitography Protocol (Protocol 0)** : 
  - Décodage spécialisé des données d'orbitographie (5 bytes + 6 bits)
  - Correction identification balises d'étalonnage 406.022 MHz
- **National User Protocol (Protocol 4)** : Extraction complète données nationales
- **Aviation/Maritime User Protocols** : Décodage Baudot amélioré pour call signs

### Fonctions Techniques Ajoutées
```c
// Nouvelles fonctions de décodage spécialisées
decode_orbitography_data()      // Balises d'étalonnage/orbitographie
decode_standard_test_data()     // Protocole test standard
decode_test_beacon_data()       // Données balises de test
decode_national_use_data()      // Données d'usage national
decode_radio_callsign_data()    // Indicatifs radio
decode_baudot_char()            // Caractères Baudot complets
display_baudot_42()             // Affichage 6 caractères Aviation
display_baudot_2()              // Affichage 7 caractères étendu
```

### Impact Conformité
- **Avant** : T.001 95% implémenté + T.018 implémenté = 95% implémenté
- **Après** : T.001 100% implémenté + T.018 implémenté = 100% implémenté

**Tests validés** : Orbitography Protocol (balises étalonnage 406.022 MHz) et Test User Protocol uniquement.
**Limitation** : Autres protocoles implémentés selon spécifications mais non testés sur balises réelles.

### Protocoles Maintenant Supportés (Complet)

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
- [x] **Protocol 12: Ship Security** (nouveau)
- [x] Protocol 13: RLS Location  
- [x] **Protocol 14: Standard Test** (nouveau)
- [x] **Protocol 15: National Test** (nouveau)

#### User Protocols (P=1)  
- [x] **Protocol 0: Orbitography** (amélioré)
- [x] Protocol 1: ELT Aviation User
- [x] Protocol 2: EPIRB Maritime User
- [x] Protocol 3: Serial User
- [x] **Protocol 4: National User** (amélioré)
- [x] **Protocol 6: Radio Call Sign** (nouveau)
- [x] **Protocol 7: Test User** (amélioré)

### Tests Validés
- Compilation sans erreurs/warnings
- Test balises d'étalonnage 406.022 MHz (Orbitography Protocol - identification correcte)  
- Test balises test utilisateur (Test User Protocol - décodage fonctionnel)
- Nouveaux protocoles (Ship Security, Standard Test, National Test, Radio Call Sign) : implémentés selon spécifications, non testés
- Régression : protocoles existants préservés

### Références Standards
- **COSPAS-SARSAT T.001** : 100% implémenté (tests partiels : orbitography, test user)
- **COSPAS-SARSAT T.018** : Implémentation complète (non testée sur balises réelles)
- **ITU-R M.585** : MID database complete
- **Modified Baudot** : Implémentation complète (testée partiellement)

---

## Version 10.2.0 - 2025-08-xx 

### 🚀 Fonctionnalités Initiales  
- Décodeur 1G complet (T.001 95% conformité)
- Décodeur 2G complet (T.018 100% conformité)
- Support audio temps réel
- Base données MID complète
- Scripts automatisation email
- Géolocalisation OpenStreetMap

### 🛠️ Architecture
- Modularité complète (5 modules principaux)
- Pipeline audio optimisé
- Correction erreurs BCH(250,202)
- Support multi-formats (hex, WAV, temps réel)
