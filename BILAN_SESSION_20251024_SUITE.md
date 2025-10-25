# Bilan Session 24/10/2025 (Suite) - Tentatives Correction Détection Préambule

**Date:** 24 octobre 2025 (soirée)
**Problème:** Préambule détecté au mauvais endroit après correction bug float32
**Objectif:** Aligner détection dec406_iq avec test_sample_rate (75.8% @ index 65)
**Résultat:** **NON RÉSOLU** - 4 tentatives, corrélation max 18.8%

---

## Contexte Initial

Après correction du bug float32 dans SARSAT_SGB (fichiers complets 2.5M samples), nouvelle investigation :

- **test_sample_rate** : Préambule @ index **65**, corrélation **75.8%** ✅
- **dec406_iq** : Préambule @ index **1,233,180**, corrélation inconnue ❌

**Objectif:** Comprendre et corriger cette différence.

---

## Tentative Fix #1 : Augmenter preamble_length

### Changement
```c
// AVANT:
const int preamble_length = 500;  // Trop court (7.7 chips seulement)

// APRÈS:
const int preamble_chips = 6400;  // 25 bits × 256 chips
const int preamble_length = preamble_chips * sps;  // 416,666 @ 2.5MHz
```

### Résultat
- **Corrélation : 6.9%** @ index -1 ❌
- **Temps calcul : 15 minutes**
- **Conclusion : ÉCHEC** - Pire qu'avant !

### Analyse
Le problème n'était pas seulement la longueur, mais la méthode de génération du préambule.

---

## Tentative Fix #2 : Chips + Upsample (copie test_sample_rate)

### Découverte Root Cause

**test_sample_rate (correct) :**
```c
generate_preamble_chips(chips);           // 6,400 chips
upsample_preamble(chips, ..., sps);       // Zero-order hold
```

**dec406_iq Fix #1 (incorrect) :**
```c
generate_preamble_reference(ref, 416666);  // Génère samples directement
// Problème: seulement 128 chips/bit au lieu de 256
// Pas d'upsampling correct
```

### Changements (dsss_demod.c)

**1. Nouvelle fonction `generate_preamble_chips()` (lignes 422-469)**
```c
static int generate_preamble_chips(float complex *preamble_chips) {
    prn_state_t prn_state_i, prn_state_q;
    prn_init(&prn_state_i, 0);
    prn_init(&prn_state_q, 0);

    // 25 bits I + 25 bits Q = 6,400 chips
    for (int bit = 0; bit < DSSS_PREAMBLE_LENGTH / 2; bit++) {
        prn_generate_i(&prn_state_i, prn_i_buf);
        prn_generate_q(&prn_state_q, prn_q_buf);

        int bit_value_i = (bit * 2) % 2;
        int bit_value_q = (bit * 2 + 1) % 2;

        // 256 chips complets par bit
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            float chip_i = (bit_value_i == 0) ? (float)prn_i_buf[c] : -(float)prn_i_buf[c];
            float chip_q = (bit_value_q == 0) ? (float)prn_q_buf[c] : -(float)prn_q_buf[c];
            preamble_chips[chip_idx++] = chip_i + I * chip_q;
        }
    }
}
```

**2. Nouvelle fonction `upsample_preamble()` (lignes 471-494)**
```c
static int upsample_preamble(const float complex *preamble_chips, int num_chips,
                             float complex *preamble_samples, float sps) {
    int num_samples = (int)(num_chips * sps);

    // Zero-order hold
    for (int i = 0; i < num_samples; i++) {
        int chip_idx = (int)(i / sps);
        preamble_samples[i] = preamble_chips[chip_idx];
    }
}
```

### Validation
Test de comparaison chips générés :
```bash
gcc compare_preambles.c prn_generator.c -lm
./compare_preambles
# Résultat: 0 différences / 6400 chips ✅
```

### Résultat Fix #2
- **Corrélation : 18.8%** @ index **390**
- **Temps calcul : 13 minutes**
- **Conclusion : AMÉLIORATION** mais encore loin de 75.8%

---

## Tentative Fix #3 : Délai OQPSK sur Préambule

### Hypothèse
test_sample_rate applique le délai OQPSK (Tc/2) au **préambule de référence**, pas au signal.

### Changement (dsss_demod.c, lignes 538-552)
```c
// Upsample QPSK chips
upsample_preamble(preamble_chips, num_preamble_chips, preamble_qpsk, sps);

// Appliquer délai OQPSK (Tc/2 sur canal Q)
int delay = (int)(sps / 2.0f);
for (int i = 0; i < preamble_length; i++) {
    float i_val = crealf(preamble_qpsk[i]);
    float q_val = (i >= delay) ? cimagf(preamble_qpsk[i - delay]) : 0.0f;
    preamble_ref[i] = i_val + I * q_val;
}

// Corréler signal OQPSK avec référence OQPSK (pas de conversion signal)
correlate_signals(&samples[idx], preamble_shifted, preamble_length);
```

### Résultat Fix #3
- **Corrélation : 18.8%** @ index **390**
- **Temps calcul : 15 minutes**
- **Conclusion : AUCUN CHANGEMENT** - Le délai OQPSK n'était pas le problème

---

## Tentative Fix #4 : Normalisation Correcte

### Découverte

**test_sample_rate :**
```c
return cabsf(corr_sum) / sqrtf(power1 * power2);  // Normalisation standard
```

**dec406_iq (avant Fix #4) :**
```c
return cabsf(corr) / len;  // Normalisation incorrecte
```

### Changement (dsss_demod.c, lignes 496-523)
```c
static float correlate_signals(const float complex *sig1, const float complex *sig2,
                               size_t len) {
    float complex corr_sum = 0.0f;
    float power1 = 0.0f, power2 = 0.0f;

    for (size_t i = 0; i < len; i++) {
        corr_sum += sig1[i] * conjf(sig2[i]);
        power1 += cabsf(sig1[i]) * cabsf(sig1[i]);
        power2 += cabsf(sig2[i]) * cabsf(sig2[i]);
    }

    if (power1 > 0 && power2 > 0) {
        return cabsf(corr_sum) / sqrtf(power1 * power2);
    }
    return 0.0f;
}
```

### Résultat Fix #4
- **Corrélation : 13.6%** @ index **390** ❌
- **Temps calcul : 40+ minutes** (3× plus lent)
- **Conclusion : PIRE QU'AVANT !** - Normalisation standard contre-productive

---

## Optimisation : Réduction Plage Fréquences

### Problème
Tests avec 161 fréquences (-12kHz à +12kHz, pas 150Hz) trop longs :
- Fix #2/3 : ~15 minutes
- Fix #4 : 40+ minutes (normalisation complexe)

### Solution Temporaire
```c
// dsss_demod.h
#define DSSS_MAX_DOPPLER  0  // Test 0 Hz uniquement (était 12000)
```

**Résultat :** Tests en ~30 secondes (161× plus rapide)

---

## Récapitulatif Résultats

| Fix | Méthode | Corrélation | Index | Temps | Status |
|-----|---------|-------------|-------|-------|--------|
| Avant | preamble_length=500 | ? | 1,233,180 | <1s | ❌ |
| #1 | preamble_length=416k | 6.9% | -1 | 15min | ❌ |
| #2 | Chips + Upsample | **18.8%** | 390 | 13min | 🟡 |
| #3 | + Délai OQPSK | 18.8% | 390 | 15min | 🟡 |
| #4 | + Normalisation | **13.6%** | 390 | 40min | ❌ |
| **test_sample_rate** | Référence | **75.8%** | **65** | ~2min | ✅ |

**Meilleur résultat dec406_iq :** Fix #2/3 avec **18.8%** (encore 4× trop faible)

---

## Fichiers Modifiés

### dsss_demod.c
- **Lignes 422-469** : `generate_preamble_chips()` (copié test_sample_rate)
- **Lignes 471-494** : `upsample_preamble()` (zero-order hold)
- **Lignes 496-523** : `correlate_signals()` (normalisation power-based)
- **Lignes 517-552** : Appel modifié dans `dsss_detect_preamble()`

### dsss_demod.h
- **Ligne 54** : `DSSS_MAX_DOPPLER = 0` (temporaire, pour tests rapides)

### Documentation
- `/tmp/preamble_length_issue.md` : Analyse initiale
- `/tmp/preamble_fix_v2.md` : Détail Fix #2
- `/tmp/compare_preambles.c` : Outil validation génération chips
- `BILAN_SESSION_20251024.md` : Investigation précédente (float32)
- `BILAN_SESSION_20251024_SUITE.md` : Ce document

---

## Analyse Finale

### Ce Qui Fonctionne ✅
1. Génération chips : **identique** à test_sample_rate (0 différences)
2. Compilation et exécution sans crash
3. Réduction temps test (0 Hz uniquement)

### Ce Qui Ne Fonctionne Pas ❌
1. **Corrélation trop faible** : 18.8% vs 75.8% attendu
2. **Index incorrect** : 390 au lieu de 65
3. **Normalisation paradoxale** : power-based donne de pires résultats
4. **Temps calcul** : 13-40 minutes pour tests complets

### Différences Non Élucidées

Malgré alignement progressif sur test_sample_rate :
- ✅ Génération chips identique
- ✅ Upsampling copié
- ✅ Délai OQPSK appliqué
- ✅ Normalisation identique
- ❌ **Résultat toujours 4× trop faible**

**Il reste une différence subtile non identifiée.**

---

## Prochaines Étapes

### Investigation Approfondie
1. **Comparer byte-par-byte** les préambules générés (après upsample + OQPSK)
2. **Vérifier step de corrélation** : dec406_iq utilise `idx += sps`, test_sample_rate peut être différent
3. **Analyser search_length** : dec406_iq cherche dans 50%, test_sample_rate peut être différent
4. **Rollback normalisation** : revenir à `/ len` (donnait 18.8% vs 13.6%)

### Tests Complémentaires
1. Test avec fichier 384kHz (plus petit, tests plus rapides)
2. Générer préambule de référence avec test_sample_rate et le sauvegarder
3. Comparer visuellement les signaux (gnuplot)

### Optimisation
1. Paralléliser recherche fréquences (OpenMP)
2. Utiliser FFT pour corrélation (O(n log n) vs O(n²))
3. Réduire plage Doppler (±3kHz au lieu de ±12kHz)

---

## Configuration Actuelle

```bash
# Compiler (avec 0 Hz uniquement)
gcc -O2 -Wall main_iq.c dsss_demod.c prn_generator.c dec406_v2g.c \
    dec406_v1g.c dec406.c display_utils.c -o dec406_iq -lm

# Tester (rapide)
./dec406_iq test_complete_2.5MHz.iq -s 2500000

# Avant de commiter: restaurer DSSS_MAX_DOPPLER
# dsss_demod.h ligne 54: 0 → 12000
```

---

## Conclusion

**Problème non résolu** malgré 4 tentatives méthodiques.

La corrélation reste 4× trop faible (18.8% vs 75.8%) et l'index est incorrect (390 vs 65). Il existe une différence subtile entre test_sample_rate et dec406_iq qui n'a pas été identifiée malgré l'alignement du code.

**Recommandation :** Investigation byte-level des signaux générés pour identifier la divergence exacte.
