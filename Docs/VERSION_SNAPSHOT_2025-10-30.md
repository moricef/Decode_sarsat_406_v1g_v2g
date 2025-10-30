# VERSION SNAPSHOT - 30 Octobre 2025

**Commit**: `d513505` - debug: Add extensive instrumentation and analysis tools for DSSS demod
**Branch**: `feature/openmp-parallel`
**Date**: 2025-10-30 16:51:29 +0100

---

## 📊 ÉTAT DU DÉMODULATEUR

### Métriques de Performance

| Métrique | Valeur | Cible | Statut |
|----------|--------|-------|--------|
| **Corrélation préambule** | 68% | >90% | 🟡 Partiel |
| **Corrélation despread** | 3.2% | >70% | 🔴 Échec |
| **Précision bits** | 55.3% | >95% | 🔴 Échec |
| **Symboles récupérés** | 33,079 | 38,400 | 🔴 -13.8% |
| **SNR estimé** | 1.5 dB | >15 dB | 🔴 Trop bas |
| **Phase détectée** | 46° | 45° | 🟢 OK |

### Bugs Actifs

#### 🔴 **BUG #1**: Timing Recovery (dsss_demod.c:970-1124)
- **Symptôme**: Perte de 5,321 symboles (-13.8%)
- **Impact**: SNR très faible (1.5 dB sur signal parfait)
- **Code**: `dsss_timing_recovery_corrected()` implémenté mais non testé
- **Priorité**: CRITIQUE

#### 🔴 **BUG #2**: Phase Ambiguity Resolution (dsss_demod.c:1187-1730)
- **Symptôme**: Teste phase sur symboles spreadés (incorrect)
- **Impact**: Détection aléatoire (~50% corrélation)
- **Solution**: Désétaler d'abord, puis tester pattern [0,1,0,1,...]
- **Priorité**: HAUTE

#### 🔴 **BUG #3**: DSSS Despreading (dsss_demod.c:1928-2050)
- **Symptôme**: Corrélation 3.2% (vs 68% sur préambule)
- **Impact**: Perte de 250 bits d'information/BCH
- **Hypothèse**: Dérive de synchronisation après préambule
- **Priorité**: CRITIQUE

#### 🟡 **BUG #4**: Costas Loop (DÉSACTIVÉ)
- **Statut**: Désactivé à la ligne 2282
- **Raison**: Divergence (+10.6 kHz sur signal -0.164 kHz)
- **Priorité**: BASSE (optionnel)

---

## 🛠️ MODIFICATIONS PRINCIPALES

### dsss_demod.c (+1082 lignes)

**Nouvelles fonctionnalités**:
- Multiples variantes OQPSK→QPSK avec traces debug (lignes 83-141)
- Timing recovery corrigé avec `phase_increment` (ligne 1033)
- Debug détaillé timing recovery (lignes 1071-1076)
- Traces phase ambiguity resolution (ligne 1470, 1709)
- Correction amplitude pour signaux faibles
- Analyse distribution constellation I/Q

**Traces de debug ajoutées**:
```c
[TIMING DEBUG] symbol[%zu]: center_idx=%d (phase=%.3f, mu=%.3f)
[PHASE DEBUG] Algorithm found: angle=%.0f°, swap=%d, invert=%d
[DEBUG] Despread params: chip_conv=%d, prn_conv=%d, offset=%d
```

### dsss_demod.h

**Nouveaux champs** dans `dsss_demod_state_t`:
```c
int auto_conv_i;              // Convention I détectée auto
int auto_conv_q;              // Convention Q détectée auto
float auto_conv_correlation;  // Corrélation convention auto
```

### main_iq.c

- **FIX**: Intégration `decode_beacon()` depuis `dec406.c`
- Suppression du stub, utilisation décodeur BCH complet
- Ajout include `dec406.h`

---

## 🔬 NOUVEAUX OUTILS D'ANALYSE

### Analyseurs Compilés

| Outil | Fichier | Fonction |
|-------|---------|----------|
| **analyze_bit_by_bit** | analyze_bit_by_bit_correlation.c | Corrélation bit-par-bit |
| **compare_tx_rx_chips** | compare_tx_rx_chips.c | Comparaison chips TX/RX |
| **generate_perfect_signal** | generate_perfect_signal.c | Signal test parfait |
| **generate_preamble_only** | generate_preamble_only.c | Préambule isolé |
| **generate_test_signal_OQPSK** | generate_test_signal_OQPSK.c | Signal OQPSK test |

### Scripts Python (tools/)

| Script | Fonction |
|--------|----------|
| **analyze_constellation.py** | Visualisation constellation I/Q |
| **verify_tx_signal.py** | Validation signal TX |
| **analyze_spectrum.py** | Analyse spectrale (déplacé) |
| **compare_bits.py** | Comparaison bits (déplacé) |
| **test_all_phases.py** | Test exhaustif phases (déplacé) |
| **quick_hex_extract.py** | Extraction hex rapide |

### Fichiers de Référence

| Fichier | Contenu |
|---------|---------|
| **expected_bits.bin** | Séquence bits attendue (300 bits) |
| **expected_bits_FIXED.bin** | Séquence bits corrigée |
| **perfect_chips.bin** | Chips PRN parfaits |
| **preamble_chips.bin** | Chips préambule seuls |
| **test_bits_reference.bin** | Référence validation |
| **correlation_per_bit.csv** | Corrélation par bit (300 entrées) |
| **timing_positions.txt** | Positions timing recovery |

---

## 📚 DOCUMENTATION AJOUTÉE

### docs_sessions/

| Document | Contenu |
|----------|---------|
| **analyses_deepseek_10282025.txt** | Analyses techniques (311 lignes) |
| **calculs_OQPSK.txt** | Calculs OQPSK détaillés (78 lignes) |
| **double_buffering_demod_103020251055.txt** | Stratégie double buffering (202 lignes) |

### Docs/

| Document | Statut |
|----------|--------|
| **ARCHITECTURE.html** | Déplacé depuis racine |
| **SESSION_SUMMARY_2025-10-26.txt** | Mis à jour |
| **VERSION_SNAPSHOT_2025-10-30.md** | ⭐ CE FICHIER |

---

## 🔄 PROCHAINES ÉTAPES

### Priorité 1 (CRITIQUE)

1. ✅ **Compiler et tester timing recovery corrigé**
   - Vérifier si 38,400 symboles sont récupérés
   - Mesurer SNR après correction
   - Fichier: `dsss_demod.c:1024-1124`

2. 🔍 **Analyse corrélation bit-par-bit**
   - Utiliser `analyze_bit_by_bit` pour tracer la chute
   - Identifier à quel bit la corrélation s'effondre
   - Comparer préambule (bits 0-49) vs info (bits 50-299)

3. 🔬 **Comparaison chips TX/RX**
   - Utiliser `compare_tx_rx_chips` sur premiers 256 chips
   - Vérifier alignement PRN
   - Détecter offset ou drift

### Priorité 2 (HAUTE)

4. 🛠️ **Refactorer phase ambiguity resolution**
   - Désétaler préambule AVANT test phase
   - Tester pattern [0,1,0,1,...] sur bits désétalés
   - Fichier: `dsss_demod.c:1187-1730`

5. 📊 **Visualisation constellation**
   - Utiliser `tools/analyze_constellation.py`
   - Vérifier points QPSK aux bons emplacements
   - Identifier rotations ou distorsions

### Priorité 3 (MOYENNE)

6. 📈 **Investigation SNR anormal**
   - Tracer puissance signal vs bruit
   - Vérifier estimation SNR (fonction `estimate_snr`)
   - Comparer avec mesures théoriques

7. 🔧 **Optimisation métrique corrélation**
   - Remplacer `fmaxf` asymétrique
   - Implémenter weighted average
   - Test sur signal parfait

---

## 📊 HISTORIQUE COMMITS RÉCENTS

```
d513505 (HEAD) debug: Add extensive instrumentation and analysis tools
bc5ce96        docs: Reorganize documentation into docs_sessions/
03da33d        docs: Add executive session summary (ASCII)
2b66b74        docs: Update README with Phase 1 fix summary
925961d        docs: Add detailed debugging session (26 Oct 2025)
29a0661        CRITICAL FIX: Phase 1 ambiguity (3 major bugs) ⭐
8244867        Fix: Apply T.018 preamble spec and diagnostic params
f6cf73f        Add full OpenMP parallelization
```

---

## 🎯 OBJECTIFS FINAUX

| Objectif | Actuel | Cible | Progression |
|----------|--------|-------|-------------|
| **Décodage BCH** | ❌ 0% | ✅ 100% | ▱▱▱▱▱▱▱▱▱▱ 0% |
| **Corrélation despread** | 3.2% | 70%+ | ▰▱▱▱▱▱▱▱▱▱ 5% |
| **Précision bits** | 55.3% | 95%+ | ▰▰▰▰▰▰▱▱▱▱ 58% |
| **Récupération symboles** | 86.2% | 100% | ▰▰▰▰▰▰▰▰▱▱ 86% |
| **SNR estimé** | 1.5 dB | 15+ dB | ▱▱▱▱▱▱▱▱▱▱ 10% |

**Statut global**: 🔴 **NON FONCTIONNEL** - Debug actif

---

## 📞 INFORMATIONS TECHNIQUES

**Compilé avec**:
```bash
gcc -o dec406_iq main_iq.c dsss_demod.c prn_generator.c dec406.c \
    dec406_v1g.c dec406_v2g.c display_utils.c -lm -O2 -fopenmp
```

**Taille binaire**: 110 KB (vs 84 KB version précédente)

**Dépendances**:
- OpenMP (parallélisation)
- libm (math)
- Standard C library

**Fichiers de test**:
- `test_known.iq` - Trame connue parfaite
- `perfect_interpolated.iq` - Signal parfait interpolé
- `burst_*.iq` - Bursts extraits

---

## 🏷️ TAGS & MÉTADONNÉES

**Keywords**: DSSS, OQPSK, COSPAS-SARSAT, T.018, démodulation, timing recovery, phase ambiguity, despreading, PRN, BCH

**État**: Work in Progress (WIP)
**Tests**: ❌ Non passants
**CI/CD**: N/A
**Production**: ❌ Non déployable

---

*Document généré automatiquement le 30 octobre 2025*
*Version tracking commit: `d513505`*
