# CLAUDE.md — dec406 COSPAS-SARSAT 406 MHz Decoder

## Méthodologie debug

Quand un bug visuel ou comportemental est signalé :

0. **Attendre la validation avant de coder.** Après analyse et proposition, TOUJOURS s'arrêter et attendre que l'utilisateur lise et valide explicitement avant de toucher au code. Ne jamais enchaîner analyse → code dans la même réponse.
1. **Diagnostiquer AVANT de coder.** Émettre une hypothèse claire. Proposer un test minimal pour la valider (désactiver une feature, ajouter un log, simplifier le rendu). Ne pas coder de fix tant que la cause n'est pas isolée.
2. **Maximum 2 tentatives de fix.** Si le 2e fix ne résout pas le problème, s'arrêter. Repenser le diagnostic depuis zéro. Ne pas empiler des workarounds.
3. **Les commits doivent être rédigés en anglais.**
4. **Ne pas committer du code de debug ou des tentatives non validées.** Ne committer que des fixes confirmés par l'utilisateur après test sur le matériel.
5. **Lire et comprendre les primitives avant de les utiliser.**
6. **Surgical Code Edits.** Prioritize minimal changes. `oldText` must match exactly including whitespace.
7. **Session Context and Memory.** LLMs do not retain memory between sessions. Any critical information must be documented explicitly.

## Projet

Démodulateur/décodeur COSPAS-SARSAT 406 MHz pour balises FGB (1G) et SGB (2G DSSS/OQPSK).

- **Branches actives** :
  - `feature/fll-pll-tracking` — tracking cohérent (FLL+PLL+DLL+Kalman), **OTA OK** (z=39.8, BCH 6 erreurs corrigées sur fichier May 6)
  - `feature/open-loop-demod` — estimation préambule one-shot (freq + phase), plus simple mais mêmes perfs OTA
- **Build** : `make build/dec406_iq`
- **Modulateur Pluto** : `/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/` (branche `feature/half-sine`)

## Architecture

### Branche `feature/fll-pll-tracking`

```
Chaîne de réception (src/dsss_demod.c) :
  1. freq_acq_coarse_fft() — 4th-power FFT at sample rate
  2. OQPSK delay: Q channel advanced by SPS/2 (integer)
  3. DC blocker (IIR, alpha=0.001)
  4. Tracking loop (FLL+PLL+DLL+Kalman) → chip-rate output
  5. despread_burst()

Tracking loop (src/tracking.c) :
  - EPL correlators (Early/Prompt/Late × PRN I/Q) at every sample
  - DLL: normalized E-L discriminator + 1st-order code loop filter
  - FLL: cross-product discriminator + 2nd-order carrier loop filter
  - PLL: Costas discriminator — actif dans tous les états ATC (gain 0.25 ACQ, 0.5 LOCK1+)
  - ATC: 3-state Adaptive Switching Control (ACQ→LOCK1→LOCK2)
  - Kalman: 5-state joint carrier/code filter (src/kalman5.c)
  - Lock detector: NBP/WBP ratio over 20-epoch window
  - Output: chip-rate samples at half-sine peak (code_phase + 0.5)
```

### Branche `feature/open-loop-demod`

```
Chaîne simplifiée :
  1. freq_acq_coarse_fft() → coarse_freq_hz
  2. OQPSK delay (integer SPS/2)
  3. DC blocker
  4. NCO wipe-off at sample rate (coarse_freq_hz)
  5. Fixed-phase decimation (phi=SPS/2)
  6. Preamble sync + phase/freq estimation from 2 halves (3200 chips each)
  7. Chip-rate phase+freq correction
  8. despread_bits()
```

Fichiers :
  src/dsss_demod.c     → Chaîne principale
  src/tracking.c       → Tracking loop (tracking branch only)
  src/kalman5.c        → Kalman filter (tracking branch only)
  src/freq_est.c       → Kay estimator + phase estimator (open-loop branch only)
  src/freq_acq.c       → Coarse FFT 4th-power
  src/despread.c       → Soft correlation ±1, sync 2-pass I/Q, 256 chips/bit
  src/main_iq.c        → Point d'entrée, sliding window, formats -u/-i/-I/-f
  include/timing_recovery.h → Gardner TED (src/timing_recovery.c) — non intégré

Gardner TED (non intégré) :
  Porté depuis 1d335b7. Farrow interpolator + Gardner TED + PI loop filter.
  Gère l'alignement OQPSK fractionnaire ET le timing recovery.
  Prochaine étape : remplacer l'OQPSK delay manuel + décimation fixe par le TED.

## Résultats OTA

| Fichier | LO | z-score | BCH | Branche |
|---------|-----|---------|-----|---------|
| May 6, 431.97 MHz | 29 MHz off | 39.8 | 6 erreurs corrigées | tracking |
| CNES 1.8 MHz | 406.053 MHz | 5.0 | échec | tracking |
| May 7, 403 MHz | on-frequency | 35.7 | échec | tracking |
| May 9, 403 MHz | on-frequency | 11.0 | échec | open-loop |

## Commandes principales

```bash
make build/dec406_iq

# Synthétique (test de régression obligatoire avant commit)
./build/dec406_iq ../../GNURADIO/test_sgb_halfsine.sigmf-data -s 2457600

# OTA SDRangel ci32_le
./build/dec406_iq sdrangel_403000_*.sigmf-data -s 2457600 -I

# CNES int16 (SPS=46.875)
./build/dec406_iq fichier.raw -s 1800000 -i -f 3053000
```

## Hardware

- **PlutoSDR** : TX 403 MHz, 2.4576 MHz, gain -g 0 (max TX)
- **RTL-SDR** : Nooelec NESDR SMArTee v5
- **Distance antennes** : 12 cm
- **Format SDRangel** : ci32_le (int32 complex little-endian), normalisation ÷2^18 dans main_iq.c

## Tentatives échouées (NE PAS RÉESSAYER)

1. **TED avec q_delay=0 après OQPSK manuel** : double alignement Q → Farrow sans signal à interpoler. Le TED doit être utilisé SEUL, AVEC q_delay=SPS/2, SANS l'étage OQPSK manuel préalable.
2. **Kay/SKD frequency estimator** : valeurs erratiques (-4500..+4700 Hz) sur OTA. Remplacé par estimation 2 moitiés de préambule.
3. **Matched filter integrate-and-dump** : dégrade le SNR vs peak-sampling. Le half-sine a son maximum au pic d'échantillonnage.
4. **freq_acq_sweep / freq_acq_from_alignment** : non fonctionnels.

## Règles utilisateur

- Le Pluto tourne en permanence, géré par l'utilisateur
- `-g 0` pour le Pluto = gain TX max (pas le RTL-SDR)
- Ne pas faire de timeouts de 10 minutes
- Le synthétique doit TOUJOURS fonctionner (test de régression obligatoire avant commit)
- Les commits signés `morel` sont faits par Claude/DeepSeek
