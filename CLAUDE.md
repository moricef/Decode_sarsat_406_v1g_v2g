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

- **Branche active** : `feature/fll-pll-tracking`
- **Build** : `make build/dec406_iq`
- **Modulateur Pluto** : `/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/` (branche `feature/half-sine`)

## Architecture — Chaîne de réception

```
src/dsss_demod.c : orchestre la chaîne complète
  1. DC blocker (IIR alpha=0.001) sur échantillons bruts
  2. freq_acq_coarse_fft() — 4th-power FFT, ±25 kHz sanity check
  3. Sweep fallback (±300 Hz PRN correlation) si FFT rejeté
  4. OQPSK delay (Q avancé de SPS/2)
  5. Tracking loop (FLL+PLL+DLL+Kalman) → chip-rate output
  6. despread_burst() — preamble sync + bit extraction

src/tracking.c : Tracking loop sample-rate
  - EPL correlators (Early/Prompt/Late × PRN I/Q) à chaque échantillon
  - DLL: normalized E-L discriminator + 1st-order code loop filter
  - FLL: cross-product discriminator + 2nd-order carrier loop filter
  - PLL: Costas discriminator (actif tous états, gain réduit en ACQ)
  - ATC: 3-state Adaptive Switching Control (ACQ→LOCK1→LOCK2)
  - Kalman: 5-state joint carrier/code (src/kalman5.c)
  - Lock detector: NBP/WBP ratio over 20-epoch window
  - Output: chip-rate samples at half-sine peak (code_phase + 0.5)

src/freq_acq.c : Coarse FFT + sweep fallback
  - 4th-power FFT (N=131072, DC guard 3 kHz)
  - Adaptive window positioning (power-based scan)
  - Sweep: 4096-chip PRN correlation, 4 Hz step, ±300 Hz

src/despread.c : Preamble sync + soft despread
  - 2-pass I/Q sync, 6400-chip preamble correlation
  - Costas 4-phase ambiguity handling
  - 256 chips/bit soft despread

src/kalman5.c : 5-state Kalman filter
  - States: [phase, freq, freq_rate, code_phase, code_rate]
  - Carrier→code coupling via f_chip/f_carrier ratio

src/main_iq.c : Point d'entrée, sliding window, formats -u/-i/-I
```

## Fichiers clés (924 lignes total tracking)

| Fichier | Lignes | Rôle |
|---------|--------|------|
| `src/tracking.c` | 611 | Boucle sample-rate, EPL, discriminateurs, ATC |
| `src/kalman5.c` | 108 | Kalman 5×5 (optionnel, activé en LOCK1) |
| `include/tracking.h` | 147 | Structs et API tracking |
| `include/kalman5.h` | 58 | Structs et API Kalman |

## Flux de données tracking loop

```
samples[n]  (post-OQPSK, post-DC-block)
    │
    ▼
┌─────────────────────────────────────────┐
│ Carrier wipeoff: bb = sample * exp(-jφ) │  ← NCO initialisé avec coarse_freq_hz
├─────────────────────────────────────────┤
│ Code NCO: code_phase += 1/sps           │
│   cur_chip = int(code_phase)            │
│   cur_peak = int(code_phase + 0.5)      │
├─────────────────────────────────────────┤
│ EPL accum (à chaque sample) :           │
│   early  += bb × PRN[chip + 0.5]        │
│   prompt += bb × PRN[chip]              │
│   late   += bb × PRN[chip - 0.5]        │
├─────────────────────────────────────────┤
│ À chaque peak crossing (half-sine pic) :│
│   → chips_out[out++] = bb               │
├─────────────────────────────────────────┤
│ À chaque epoch (coh_chips atteint) :    │
│   → FLL discrim (cross-product)         │
│   → PLL discrim (Costas)                │
│   → DLL discrim (E-L normalisé)         │
│   → Loop filters (2nd ordre carrier)    │
│   → ATC update (state transitions)      │
│   → Reset accumulators                  │
└─────────────────────────────────────────┘
    │
    ▼
chips_out[] → despread_burst()
```

## ATC — Adaptive Switching Control

| État | coh_chips | FLL | PLL | Kalman |
|------|-----------|-----|-----|--------|
| ACQ  | 64        | BW=2 Hz, gain 1.0 | gain 0.25 | Off |
| LOCK1| 128       | BW=1.5 Hz, gain 1.0 | gain 0.5 | On |
| LOCK2| 256       | Off | gain 0.5, BW=2 Hz | Stabilisé |

Transitions :
- ACQ → LOCK1 : lock > 0.35 pendant 1 epoch
- LOCK1 → LOCK2 : lock > 0.70 pendant 10 epochs
- LOCK1 → ACQ : lock < 0.25 pendant 5 epochs
- LOCK2 → LOCK1 : lock < 0.25 pendant 5 epochs

## Discriminateurs

**FLL (cross-product, insensible aux bits)** :
```
cross = Re(P[k-1]) × Im(P[k]) - Im(P[k-1]) × Re(P[k])
dot   = Re(P[k-1]) × Re(P[k]) + Im(P[k-1]) × Im(P[k])
d_freq = atan2(cross, dot) / (2π × T)
```
NB: Les transitions de bits entre epochs corrompent le cross-product
    (une inversion de bit → 150 Hz d'erreur sur le discriminateur).
    Un moving average 3-tap est appliqué pour atténuer.

**PLL (Costas QPSK)** :
```
d_phase = sign(Re(P))×Im(P) - sign(Im(P))×Re(P)
d_phase /= |P|²
```
La caractéristique est sin(4θ) avec pull-in ±45°.
Le PLL a un terme proportionnel (appliqué au phasor) ET intégral
(via carr_integrator).

**DLL (E-L normalisé)** :
```
d_tau = 0.5 × (E - L) / (E + L)  [chips]
```
Dead zone ±0.15 chip. DLL gelé si lock > 0.25.

## Lock detector

```
NBP = |Σ prompt[k] sur 20 epochs|²    (cohérent)
WBP = Σ |prompt[k]|² sur 20 epochs     (non-cohérent)
lock = NBP / (WBP × 20)

= 1.0  si parfaitement locké (phase constante entre epochs)
= 0.05 si purement bruit (1/20)
```

## Problèmes identifiés (2026-05-13)

1. **Lock indicator trop bas pour les seuils ATC** :
   - Le lock ne dépasse ~0.20 en ACQ (coh=64) même quand la fréquence est correcte (−206 Hz)
   - Les seuils LOCK1_THRESH=0.35 et UNLOCK_THRESH=0.25 sont inatteignables au SNR OTA
   - Cause : le prompt SNR par epoch de 64 chips est insuffisant pour la cohérence de phase

2. **FLL corrompu par les transitions de bits** :
   - Après le préambule (bits aléatoires), chaque transition I ou Q injecte
     jusqu'à ±150 Hz d'erreur dans le discriminateur cross-product
   - Le moving average 3-tap atténue mais ne supprime pas
   - Solution possible : FLL decision-directed ou désactiver FLL après ACQ

3. **Gains PLL très faibles** :
   - En ACQ (coh=64, T=1.67ms) : carr_alpha ≈ 0.009, carr_beta ≈ 4e-5
   - La correction de phase est proportionnelle avec un gain minuscule
   - La convergence en phase est extrêmement lente

4. **DLL gelé en early-late identique au premier chip** :
   - Initialisation code_phase=0 → premier pic à code_phase=0.5
   - Pour le premier chip, late = chip 0 = prompt (même PRN)
   - La discrimination E-L est nulle au premier chip

5. **Sweep fallback peu fiable** :
   - conf 2.6-3.8 au lieu de 38 sur synthétique
   - 151 bins → expected noise peak ~3.2, seuil à 3.0 trop juste

6. **Fichiers OTA décodés** : 0/5 sur G35_May12 avec le tracking.
   Le FFT trouve -206 Hz (conf 11000-19000) sur les fenêtres contenant le burst,
   le tracking converge bien vers -206 Hz, mais le lock indicator reste bas
   et le despread final donne z=5-8 (bruit).

## État des modifications (vs main)

Les fichiers suivants ont été modifiés par rapport à l'état initial du tracking :
- `src/despread.c` : Costas phase 1/3 bit decision fix (porté)
- `src/freq_acq.c` : FFT adaptatif + DC guard 3 kHz + COARSE_FFT_N=131072 (porté)
- `src/dsss_demod.c` : DC blocker avant FFT, sweep fallback, |f| ≤ 25 kHz

## Commandes principales

```bash
make build/dec406_iq

# Synthétique (test de régression obligatoire avant commit)
./build/dec406_iq ../../GNURADIO/test_sgb_halfsine.sigmf-data -s 2457600

# OTA SDRangel ci32_le
./build/dec406_iq sdrangel_403000_*.sigmf-data -s 2457600 -I

# Debug tracking CSV
# Génère /tmp/c_tracking_epl.csv avec lock_ind, carrier_freq_hz par epoch
```

## Hardware

- **PlutoSDR** : TX 403 MHz, 2.4576 MHz, gain -g 0
- **RTL-SDR** : Nooelec NESDR SMArTee v5
- **Distance antennes** : 12 cm
- **Format SDRangel** : ci32_le (int32 complex little-endian), normalisation ÷2^22 (4194304)

## Règles utilisateur

- Le Pluto tourne en permanence, géré par l'utilisateur
- `-g 0` pour le Pluto = gain TX max (pas le RTL-SDR)
- Ne pas faire de timeouts de 10 minutes
- Le synthétique doit TOUJOURS fonctionner (test de régression obligatoire avant commit)
- Les commits signés `morel` sont faits par Claude/DeepSeek
- RÈGLE ABSOLUE : Le signal OTA SGB est FORT et visible. Ne JAMAIS invoquer un SNR insuffisant
  ou un signal trop faible. Le problème est dans le décodeur, pas dans le signal.
