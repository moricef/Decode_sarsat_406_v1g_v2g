# Session de Debugging dec406 - 26 Octobre 2025

## Contexte
Décodage SARSAT T.018 2G avec dec406_v10.2 montrant une corrélation très faible (2.5%) malgré un signal IQ généré correctement par SARSAT_SGB.

## Symptômes Initiaux
- **Corrélation Phase 1** : 89% (semblait bon)
- **Corrélation Diagnostic** : 62-66% (sur préambule)
- **Corrélation Désétalement Final** : 2.5% (très faible)
- **SNR estimé** : 1.5 dB (anormalement bas)
- **Phase détectée** : 133° puis 44° après corrections (devrait être 0/90/180/270)

## Bugs Découverts et Corrigés

### 🔴 BUG MAJEUR #1: Pattern de préambule incorrect (Phase 1)
**Fichier**: `dsss_demod.c` lignes 1083-1084
**Découvert par**: Analyse utilisateur comparant avec T.018 §2.2.4

**Code erroné**:
```c
// Expected preamble pattern
uint8_t expected_i = (i * 2) % 2;      // → 0,0,0,0,... (correct par hasard)
uint8_t expected_q = (i * 2 + 1) % 2;  // → 1,1,1,1,... ❌ FAUX!
```

**Spécification T.018 §2.2.4**:
> "I and Q component information bits shall all be set to '0' during the preamble"

**Correction**:
```c
// T.018 §2.2.4: Preamble = all bits '0' (both I and Q channels)
uint8_t expected_i = 0;
uint8_t expected_q = 0;
```

**Impact**:
- Phase 1 testait le canal Q contre un pattern inversé (tous à 1)
- Réduisait artificiellement la corrélation détectée
- Causait une mauvaise sélection des paramètres d'ambiguïté

**Résultat après correction**:
- Phase passe de 133° → 44° (proche de 45° théorique OQPSK)
- Détection `invert=1` maintenant correcte
- Corrélation Diagnostic améliore de 62% → 68%

---

### 🔴 BUG MAJEUR #2: Phase 1 teste seulement 50 chips au lieu de 12,800
**Fichier**: `dsss_demod.c` ligne 1056
**Découvert par**: Analyse Claude après résultats incohérents

**Code erroné**:
```c
// Test on first 50 symbols (preamble)
const int test_symbols = 50;
```

**Calcul correct**:
- Préambule T.018 = 50 bits
- Spreading factor = 256 chips/bit
- **Total requis = 50 × 256 = 12,800 chips**

**Correction**:
```c
// Test on first 50 bits = 12,800 chips (preamble)
// T.018: 50 bits × 256 chips/bit = 12,800 chips total
const int test_symbols = DSSS_PREAMBLE_LENGTH * DSSS_SPREADING_FACTOR;  // 12,800
```

**Impact**:
- Phase 1 ne testait que **0.4% du préambule** (50/12800)
- Trouvait une fausse corrélation de 89% sur un échantillon minuscule
- Sélectionnait de mauvais paramètres (angle, swap, invert)

**Résultat après correction**:
- Corrélation Phase 1 chute de 89% → 50.6% (plus réaliste)
- Phase détectée : 46° (cohérent avec OQPSK ±45°)
- Paramètres détectés maintenant fiables sur préambule complet

---

### 🔴 BUG MAJEUR #3: Incohérence OQPSK Tc/2 delay
**Fichier**: `dsss_demod.c` lignes 236-255 (fonction `apply_oqpsk_delay_for_corr`)
**Découvert par**: Analyse utilisateur comparant deux fonctions OQPSK

**Problème identifié**:
Deux fonctions gérant le délai OQPSK Tc/2 avec des logiques **contradictoires** :

1. **`oqpsk_to_qpsk`** (Step 4.5) : AVANCE Q de Tc/2 pour compenser le délai TX
2. **`apply_oqpsk_delay_for_corr`** : RETARDE Q de Tc/2 (simulait le TX au lieu de compenser)

**Code erroné**:
```c
float q_val = (i >= delay) ? cimagf(preamble[i - delay]) : 0.0f;  // DÉLAI
```

**Correction**:
```c
for (int i = 0; i < num_samples - delay; i++) {
    float i_val = crealf(preamble[i]);
    float q_val = cimagf(preamble[i + delay]);  // AVANCE Q(t + Tc/2)
    output[i] = i_val + I * q_val;
}
// Remplir les derniers échantillons avec des zéros
for (int i = num_samples - delay; i < num_samples; i++) {
    output[i] = 0.0f;
}
```

**Logique correcte**:
- **TX** : Envoie I(t) + j·Q(t - Tc/2) (OQPSK)
- **RX** : Doit créer I(t) + j·Q(t) pour corrélation (compensation Tc/2)
- Les deux fonctions RX doivent AVANCER Q de Tc/2 (cohérence)

**Impact**:
- Avant : Résultats de corrélation incohérents entre fonctions
- Après : Cohérence entre toutes les fonctions de compensation OQPSK

---

## Analyse du Fichier IQ (SARSAT_SGB)

### Investigation: Canal Q à zéro au début du fichier
**Observation initiale**:
```bash
$ head -c 100 test_fixed_2.5MHz.iq | od -An -t f4
I=0, Q=0
I=-2.0e-05, Q=0
I=-5.7e-05, Q=0
...
```

**Hypothèse erronnée**: Fichier IQ corrompu, canal Q manquant

**Vérification plus loin dans le fichier**:
```bash
$ dd if=test_fixed_2.5MHz.iq bs=8 skip=100000 count=50 | od -An -t f4
I=-0.743, Q=1.000
I=-0.757, Q=1.000
I=-0.771, Q=1.000
...
```

**Conclusion**:
- Le fichier IQ est **correct** !
- Le canal Q = 0 au début est dû à la **rampe du filtre RRC**
- Signal OQPSK complet apparaît après la montée du filtre
- Le décodeur détecte correctement le préambule après cette rampe

---

## Résultats Comparatifs

### Avant Corrections
```
Phase 1 : 89.0% @ angle=133°, swap=0, invert=1 (50 chips testés)
Phase 2 : 89.0% @ offset=0, prn_conv=0
Diagnostic : 62.0% @ offset=-2, prn_conv=1
Despread : 2.5% correlation (paramètres incohérents)
SNR : 1.5 dB
```

### Après Bug #1 (Pattern préambule)
```
Phase 1 : 89.0% @ angle=44°, swap=0, invert=1 (toujours 50 chips!)
Phase 2 : 89.0% @ offset=0, prn_conv=0
Diagnostic : 66.0% @ offset=-2, prn_conv=1 (+4%)
Despread : 2.5% correlation
```

### Après Bugs #1 + #2 + #3 (Tous corrigés)
```
Phase 1 : 50.6% @ angle=46°, swap=0, invert=1 (12,800 chips complets)
Phase 2 : 58.0% @ offset=+9, prn_conv=0
Diagnostic : 68.0% @ offset=+5, prn_conv=0 (+8%)
Despread : 3.2% correlation (paramètres cohérents)
SNR : 1.5 dB (inchangé)
```

---

## Problème Restant : Écart Diagnostic vs Despread

### Symptôme
- **Diagnostic** (préambule 50 bits) : **68%** corrélation ✅
- **Despread** (trame complète 300 bits) : **3.2%** corrélation ❌

### Analyse Théorique
Si on suppose :
- Préambule (50 bits) : 68% corrélation
- Information + BCH (250 bits) : ~0% corrélation (données aléatoires)

**Moyenne attendue** : `(50 × 0.68 + 250 × 0) / 300 = 11.3%`

**Moyenne observée** : 3.2%

### Hypothèses Possibles

1. **Métrique de corrélation différente**
   - Diagnostic : % bits corrects (simple comparaison)
   - Despread : Corrélation normalisée asymétrique avec `fmaxf(0, ...)`
   - Les bits <50% correlation contribuent 0 au total

2. **Dérive de synchronisation**
   - L'offset optimal (+5) pour le préambule devient incorrect après
   - Perte de synchronisation chip au fil des 300 bits
   - Nécessité d'un tracking de phase/timing continu

3. **PRN sequence drift**
   - Générateur PRN TX vs RX désynchronisés après préambule
   - Possible erreur dans l'initialisation PRN pour data vs preamble

4. **Problème fondamental timing**
   - SNR 1.5 dB reste très faible (devrait être 10-15 dB minimum)
   - Suggère un problème de récupération de timing ou AGC

---

## Actions Recommandées

### Court Terme
1. ✅ **FAIT**: Corriger les 3 bugs majeurs identifiés
2. ✅ **FAIT**: Versionner et documenter les corrections
3. 🔄 **TODO**: Ajouter trace corrélation bit-par-bit pour identifier où elle chute
4. 🔄 **TODO**: Comparer PRN généré TX vs RX sur les 64 premiers chips

### Moyen Terme
1. 🔄 Investiguer pourquoi SNR reste à 1.5 dB malgré signal propre
2. 🔄 Vérifier si timing recovery maintient la synchro sur 300 bits
3. 🔄 Tester avec signal simulé idéal (sans bruit, sans dérive)

### Long Terme
1. 🔄 Implémenter tracking de phase/timing continu (pas juste au préambule)
2. 🔄 Améliorer métrique de corrélation (weighted average, confidence intervals)
3. 🔄 Ajouter visualisation constellation I/Q à différents points du pipeline

---

## Références
- **T.018 Rev.12 Oct 2024** - Spécification COSPAS-SARSAT 2ème génération
- **SARSAT_SGB** - Transmetteur ADALM-PLUTO (implémentation TX correcte)
- **dec406_v10.2** - Récepteur/décodeur (corrections en cours)

---

## Conclusions de la Session

### Progrès Réalisés ✅
1. Identification et correction de 3 bugs critiques dans Phase 1
2. Amélioration corrélation diagnostic de 62% → 68%
3. Cohérence des paramètres entre diagnostic et désétalement
4. Compréhension de la structure du signal IQ (rampe RRC normale)

### Problèmes Persistants ⚠️
1. Corrélation finale (3.2%) reste très faible malgré préambule correct (68%)
2. SNR estimé anormalement bas (1.5 dB au lieu de 10-15 dB)
3. Écart inexpliqué entre corrélation préambule vs trame complète

### Leçons Apprises 🎓
1. **Toujours tester sur échantillon complet** - Phase 1 testait 0.4% du préambule!
2. **Vérifier cohérence entre fonctions similaires** - Deux implémentations OQPSK contradictoires
3. **Valider contre spécification** - Pattern préambule incorrect depuis le début
4. **Méfiance envers les "bons" résultats** - 89% était une fausse corrélation

---

**Auteur**: Claude Code + Utilisateur
**Date**: 26 Octobre 2025
**Durée Session**: ~3 heures
**Commits**: 1 (29a0661)
**Fichiers Modifiés**: dsss_demod.c
**Statut**: Bugs critiques corrigés, investigation en cours pour problème corrélation finale
