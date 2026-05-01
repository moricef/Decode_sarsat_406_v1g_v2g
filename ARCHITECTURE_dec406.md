# Architecture dec406 - Démodulateur/Décodeur COSPAS-SARSAT 406 MHz

**Date**: 2026-05-01
**Branche**: `feature/no-matched-filter`
**Version**: dec406_V10.2

---

## 1. État actuel

### Chaîne complète 2G (DSSS OQPSK)

```
IQ brut (2.4576 MHz)
  → delay Q de SPS/2 (compensation OQPSK)
  → FFT coarse frequency estimator (sur signal brut, ±25 kHz, 64 phases × 4 phases Costas)
  → correction fréquence sur signal brut
  → RRC matched filter (α=0.5, 705 taps, 11×SPS)
  → symbol sync (max énergie) ou phase FFT
  → Costas loop QPSK (BW=0.0628) ou bypass
  → despread (PRN sync 2-pass + majorité par bit sur 256 chips)
  → 250 bits → decode_2g() → BCH(250,202)
```

### Statut par composant

| Composant | Statut | Détail |
|-----------|--------|--------|
| Décodeur 1G (FGB) | ✅ bit-perfect | `dec406_v1g.c` |
| Décodeur 2G (SGB) | ✅ bit-perfect | `dec406_v2g.c`, BCH Berlekamp-Massey + Chien |
| Modulateur Pluto | ✅ validé | `sarsat_sgb`, constellation QPSK ±0.66 validée GR |
| Démodulateur DSSS — signal synthétique | ✅ bit-perfect | Chaîne C complète, output = input |
| Démodulateur DSSS — signal avec offset | 🟡 partiel | FFT trouve l'offset, preamble sync OK (95%), mais erreurs BCH sur message (dérive de phase résiduelle ~1-5 Hz sur 833 ms) |
| Démodulateur DSSS — signal OTA | ❌ | Signal trop faible, pas encore de décodage réussi |

### Fichiers source du démodulateur 2G

| Fichier | Rôle |
|---------|------|
| `src/dsss_demod.c` | Façade, orchestre la chaîne, FFT coarse, fft_radix2 |
| `src/rrc_filter.c` | RRC matched filter (port de GNU Radio firdes.cc) |
| `src/symbol_sync.c` | Décimation par max énergie sur le préambule |
| `src/costas4.c` | Costas loop QPSK 4e ordre (port de GR costas_loop_cc) |
| `src/despread.c` | PRN LFSR, sync 2-pass, désétalement 4 phases, API splitée |
| `src/main_iq.c` | Point d'entrée, chargement IQ, dispatch |
| `src/dec406_v2g.c` | Décodeur 2G + BCH(250,202) Berlekamp-Massey + Chien |
| `include/despread.h` | API despread (sync + bits + burst) |
| `tests/test_sgb_codec.c` | Tests unitaires BCH, PRN, message (94 tests) |

---

## 2. Chaîne de démodulation — détail des étages

### Étage 1 : Compensation OQPSK
- Délai de Q de SPS/2 = 32 échantillons (annule le décalage Tc/2 du modulateur)
- `delayed[t] = I[t] + j*Q[t-32]`

### Étage 2 : FFT coarse frequency estimator
- Décimation du signal brut à 64 phases différentes
- Pour chaque phase : 4 prédictions Costas (0°/90°/180°/270°)
- Strip modulation : `r[k] × conj(I_exp ± j*Q_exp)`
- Soustraction DC, FFT 8192 points, interpolation quadratique du pic
- Résolution ~4.7 Hz, seuil peak/mean = 15.0
- Si offset trouvé (>1 Hz) : correction appliquée au buffer brut AVANT RRC
- Si pas d'offset (ratio < 15) : chaîne inchangée (cas synthétique même horloge)

### Étage 3 : RRC matched filter
- `rrc_compute_taps()` : port exact de `gr-filter/lib/firdes.cc::root_raised_cosine()`
- 705 taps (11×SPS, forcé impair), α=0.5, gain=1.0
- `rrc_filter_complex()` : convolution centrée (délai de groupe nul)

### Étage 4 : Symbol sync + décimation
- Si FFT coarse a trouvé un offset : réutilisation du φ FFT (pas de recalcul)
- Sinon : `symbol_sync_decimate()` — max énergie sur 6400 chips de préambule
- Décimation par SPS=64 → 38400 chips/s

### Étage 5 : Costas loop (optionnel)
- Si FFT coarse a corrigé : **bypass** (le résidu ~1-5 Hz est plus petit que le transitoire d'acquisition de la Costas)
- Sinon : Costas QPSK 4e ordre, BW=0.0628, damping=√2/2

### Étage 6 : Despread
- API splitée : `despread_sync()` puis `despread_bits()`
- Sync 2-pass : I sur [0,200) chips × 4 phases, Q sur [off_I-5, off_I+5]
- Seuil : 75% de corrélation préambule (6400 chips par canal)
- Désétalement : 125 bits par canal, majorité sur 256 chips, 4 formules Costas-phase
- PRN : LFSR 23 bits, G(x)=X^23+X^18+1, seeds 0x000001 (I) et 0x1AC1FC (Q)

---

## 3. Structure du projet

```
dec406_V10.2/
├── src/                    # Sources C
│   ├── dsss_demod.c        # Façade démodulateur + FFT coarse
│   ├── rrc_filter.c        # Filtre RRC
│   ├── symbol_sync.c       # Synchro symbole
│   ├── costas4.c           # Costas loop
│   ├── despread.c          # Désétalement DSSS
│   ├── dec406_v2g.c        # Décodeur 2G + BCH
│   ├── dec406_v1g.c        # Décodeur 1G
│   ├── dec406.c            # Wrapper dispatch
│   ├── dec406_hex.c        # Entrée hex
│   ├── main_iq.c           # Entrée IQ
│   ├── main_audio.c        # Entrée audio
│   └── display_utils.c     # Affichage
├── include/                # Headers
│   ├── dsss_demod.h
│   ├── despread.h
│   ├── rrc_filter.h
│   ├── symbol_sync.h
│   ├── costas4.h
│   └── dec406.h
├── build/                  # Binaires
├── tests/                  # Tests unitaires
│   └── test_sgb_codec.c    # 94 tests (BCH, PRN, message)
├── data/                   # Fichiers IQ de test
├── scripts/                # scan406.pl, tests PRN/RRC
├── utils/                  # resample_iq, generate_2g_hex, reset_usb
├── docs/                   # Spécifications T.018, validation
│   └── archives/           # Docs obsolètes archivées
├── test_matlab_coder/      # MATLAB Coder (obsolète, non compilé)
├── archive/                # Ancienne implémentation DSSS
├── Makefile
├── README.md
└── ARCHITECTURE_dec406.md  # Ce fichier
```

---

## 4. Résultats des tests

### Signal synthétique (sarsat_sgb -o)
- Frame : `09C4745638D95999A02B33326C3EC4400003FFF00C02832000002B774C24FE4`
- Démodulation : **bit-perfect** (hex output = hex input)
- FFT coarse : ratio 9.2 < 15 → pas de correction → Costas standard

### Signal synthétique avec offset 8.5 kHz
- FFT coarse : **8499 Hz trouvé**, ratio 1144, φ=63
- Preamble sync : **94.6% I, 96.4% Q** (phase 0°)
- Message : erreurs BCH (>6 bits) à cause de la dérive de phase résiduelle (~1 Hz → 300° sur 833 ms)

### Signal OTA (Pluto → RTL-SDR)
- Non décodé à ce jour (2026-05-01)
- Le signal est reçu (~25 dB au-dessus du bruit en puissance) mais la corrélation chip reste à ~52% (niveau bruit)
- Causes probables : rapport signal/bruit insuffisant au niveau chip, offset résiduel non corrigé

---

## 5. Problèmes connus et pistes

### Priorité 1 : Dérive de phase sur le message
- **Symptôme** : 69% d'erreurs bit sur signal avec offset, malgré preamble sync à 95%
- **Cause** : 1-5 Hz résiduel après FFT coarse → rotation de 60-300° pendant les 833 ms de message
- **Pistes** : FFT fine sur tout le burst (besoin de >1s de données pour résolution <1 Hz), ou Costas initialisée avec la phase du preamble sync

### Priorité 2 : Signal OTA
- **Symptôme** : corrélation préambule à 52% (bruit)
- **Pistes** : augmenter gain Pluto/RTL-SDR, câble direct, vérifier fréquence exacte

### Priorité 3 : Glissement du main_iq.c
- `main_iq.c` ne lit que la 1ère seconde du fichier → inadapté pour un signal dont on ne connaît pas le début
- Restaurer le sliding window (commit d0d9a23) avec le bug de fenêtre corrigé

---

## 6. Références

- COSPAS-SARSAT T.018 Rev. 7 (March 2021) — SGB specifications
- COSPAS-SARSAT T.001 — FGB specifications
- GNU Radio gr-digital/lib/costas_loop_cc_impl.cc — référence Costas
- GNU Radio gr-filter/lib/firdes.cc — référence RRC
- sgb-codec Python (jbirby) — référence codec SGB
- sarsat_sgb (ADALM-PLUTO) — modulateur de test

---

**Dernière mise à jour** : 2026-05-01 — session feature/no-matched-filter
