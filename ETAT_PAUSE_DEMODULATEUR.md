# État du Projet Démodulateur DSSS/OQPSK - PAUSE TEMPORAIRE

**Date** : 2025-10-19
**Status** : ⏸️ **EN PAUSE - Non Opérationnel**
**Raison** : Bugs architecturaux profonds nécessitant expertise DSP/SDR approfondie

---

## 📊 Synthèse Rapide

**Objectif** : Démoduler signaux IQ COSPAS-SARSAT 2G (DSSS/OQPSK) → 300 bits

**Résultat Actuel** : **55.3% de bits corrects** (devrait être >95%)

**Conclusion** : Implémentation non fonctionnelle, nécessite refonte ou approche différente

---

## ✅ Ce Qui Fonctionne

### 1. Générateur de Signal (SARSAT_SGB)
**Status** : ✅ **OPÉRATIONNEL**

- Localisation : `~/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/`
- Génère frames T.018 conformes
- Encodage BCH(250,202) validé
- OQPSK avec séparation odd/even bits correcte
- Fichiers test générés : `test_sgb.iq`, `test_known.iq`
- Compilation : `make` dans le dossier
- Frame test connue :
  ```
  HEX: 89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
  TAC: 9999, Serial: 13398, Country: 227 (France)
  Position: 43.2°N, 5.4°E
  BCH: VALID
  ```

### 2. Décodeur de Frames (dec406_v2g.c)
**Status** : ✅ **OPÉRATIONNEL**

- Localisation : `~/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_v10.2/`
- BCH(250,202) validé sur frames hardware
- Décode tous champs T.018 correctement
- **MAIS** : Nécessite 300 bits corrects en entrée

### 3. Architecture du Démodulateur
**Status** : ✅ **Structure Complète** (mais non fonctionnelle)

**Fichiers créés** :
- `dsss_demod.h` (257 lignes) - API complète
- `dsss_demod.c` (~800 lignes) - Implémentation 8 étapes
- `main_iq.c` (200 lignes) - Entry point
- `prn_generator.c/h` - Copié depuis SARSAT_SGB

**Composants implémentés** :
1. ✅ AGC (Automatic Gain Control)
2. ✅ Preamble detection (avec recherche fréquence ±12 kHz)
3. ✅ Frequency correction (coarse, fine désactivée)
4. ✅ Timing recovery (Gardner TED)
5. ✅ Phase ambiguity resolution (4 rotations × 2 swaps)
6. ✅ DSSS despreading (PRN correlation XOR)
7. ✅ OQPSK demodulation
8. ✅ File loading (.iq format)

**Compilation** :
```bash
cd ~/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_v10.2
gcc -o dec406_iq main_iq.c dsss_demod.c prn_generator.c \
    dec406.c dec406_v1g.c dec406_v2g.c display_utils.c -lm -O2
```

**Exécution** :
```bash
./dec406_iq test_known.iq
```

---

## ❌ Ce Qui Ne Fonctionne PAS

### Résultats de Test

**Signal** : `test_known.iq` (frame connue, SNR parfait)

**Résultats démodulation** :

| Métrique | Attendu | Obtenu | Status |
|----------|---------|--------|--------|
| **Préambule** | 0101010101... | 60% erreurs | ❌ |
| **Bits corrects** | >95% | **55.3%** | ❌ |
| **BCH syndrome** | 0 | 0x09D3068D25467 | ❌ |
| **SNR estimé** | >20 dB | 1.1 dB | ❌ |
| **Mean correlation** | >0.7 | 0.053 | ❌ |
| **Phase rotation** | 0° (0) | 90° (1) | ❌ |

**Hex démodulé** :
```
Attendu:  89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
Obtenu:   B8D0EE0EF7AB69EB41B70F0DD30C7E1554151344C05CAB652A43418FAD68757A...
```

### Analyse Bit-à-Bit

**Tests exhaustifs effectués** (scripts Python) :
- ✅ Inversion globale des bits → 50.7% matches (pas mieux)
- ✅ Shifts de -20 à +20 bits → meilleur à -7 bits = 53.6%
- ✅ Combinaisons shift+inversion → meilleur = 55.3%
- ❌ **AUCUNE transformation simple ne donne >70% de matches**

**Conclusion** : Les erreurs ne sont PAS dues à :
- Inversion simple de chips
- Décalage simple de bits
- Rotation de phase simple

**Mais à** : Problèmes architecturaux dans le traitement du signal

---

## 🐛 Bugs Identifiés (Non Résolus)

### 1. Timing Recovery Suspect
**Symptômes** :
- 33,079 symboles récupérés (devrait être ~38,400 chips = 38,400 symboles)
- Décalage de -7 bits observé dans comparaison
- SNR très bas (1.1 dB sur signal parfait)

**Hypothèses** :
- Gardner TED mal implémenté ou biaisé
- Échantillonnage aux mauvais instants
- Interpolation incorrecte
- Loop filter mal dimensionné

**Code suspect** : `dsss_demod.c:300-350` (fonction `dsss_timing_recovery()`)

### 2. Phase Ambiguity Incorrecte
**Symptômes** :
- Rotation=1 (90°) sélectionnée avec correlation=48.6% (presque hasard)
- Warning "Phase ambiguity resolution uncertain"

**Hypothèses** :
- Phase testée sur symboles ÉTALÉS (incorrect)
- Devrait être testée APRÈS despreading sur préambule désétalé
- Les 8 combinaisons testées ne couvrent pas le bon espace

**Code suspect** : `dsss_demod.c:380-430` (fonction `dsss_resolve_phase_ambiguity()`)

### 3. DSSS Despreading Bas SNR
**Symptômes** :
- Mean correlation = 0.053 (devrait être >0.7)
- Presque du hasard (0.5)

**Hypothèses** :
- PRN chips mal synchronisés avec chips reçus
- Convention chips (+1/-1 vs 0/1) encore incorrecte
- Décalage temporel dans PRN sequence
- XOR correlation mal implémentée

**Code suspect** : `dsss_demod.c:433-540` (fonction `dsss_despread()`)

### 4. Costas Loop Divergente
**Status** : **DÉSACTIVÉE** (ligne 629-638)

**Symptômes** :
- Estimait +10.6 kHz d'offset sur signal centré à -0.164 kHz
- Divergeait au lieu de converger

**Hypothèses** :
- Loop bandwidth trop large
- Phase error detector incorrect pour QPSK
- Instabilité sur signal étalé DSSS

**Note** : Actuellement bypassée (coarse correction uniquement)

### 5. OQPSK Délai Tc/2
**Status** : Non vérifié

**Hypothèse** : Le délai demi-chip entre I et Q n'est peut-être pas géré correctement lors de la conversion symbols→chips

**Code** : `dsss_demod.c:692-697`

---

## 🔧 Corrections Tentées (Sans Succès)

1. ✅ Inversion chips : `Real>0 → 0, Real<0 → 1` (au lieu de 1/0)
2. ✅ Conversion PRN : `-1/+1 → 0/1` pour XOR
3. ✅ Recherche fréquence complète (±12 kHz, pas early exit)
4. ✅ Costas loop désactivée
5. ✅ Debug output complet (préambule, hex bits)
6. ✅ Scripts analyse Python (compare_bits.py, test_all_phases.py)

**Résultat** : Amélioration marginale (45% → 55%), toujours non fonctionnel

---

## 📚 Documentation Utile

### Références Correctes ✅
1. **T.018 Rev.12** (Spec officielle)
   - `/MPLABXProjects/SARSAT_T018_dsPIC33CK.X/Docs/Docs_COSPAS-SARSAT/2024/`
   - Section 2.2.3.b : OQPSK architecture
   - Appendix D : PRN generation
   - Appendix B : BCH(250,202)

2. **PDF MATLAB officiel** (MathWorks R2024a)
   - `/Docs/Matlab DSSS/DSSSReceiverForSARbasedTrackingSystem.pdf`
   - Architecture receiver complète
   - Algorithmes détaillés

### Références Incorrectes ❌
**NE PAS UTILISER** : `/DSSS_Complete/matlab_code/*.m`
(Erreurs LFSR, init states, architecture)

### Fichiers de Test
- `test_sgb.iq` (fichier original, paramètres inconnus)
- `test_known.iq` (frame connue générée, documentée ci-dessus)

### Scripts d'Analyse
- `compare_bits.py` - Comparaison bit-à-bit attendu vs reçu
- `test_all_phases.py` - Test transformations (inversion, shifts)
- `analyze_spectrum.py` - Analyse spectrale FFT

---

## 🛣️ Pistes pour Reprise Future

### Option A : Debug Approfondi
**Pré-requis** :
- Compétences DSP/SDR avancées
- Outils : GNU Radio avec GUI plots, MATLAB/Octave
- Patience : plusieurs jours

**Méthodologie** :
1. **Valider chaque étape séparément** avec plots :
   - AGC : vérifier normalisation puissance
   - Preamble detection : plot corrélation vs fréquence
   - Freq correction : vérifier spectre après correction
   - Timing recovery : plot TED error, symboles récupérés
   - Phase ambiguity : plot constellation QPSK
   - Despreading : plot corrélation PRN chip-by-chip

2. **Tester sur signal simplifié** :
   - Pas de Doppler (0 Hz)
   - Timing recovery désactivé (échantillonnage fixe)
   - Phase fixée (rotation=0)
   - Focus PRN despreading uniquement

3. **Comparer avec implémentation de référence** :
   - MATLAB code (si trouvable et correct)
   - GNU Radio flowgraph équivalent
   - Code GPS/GNSS DSSS receiver

### Option B : GNU Radio Blocks
**Plus rapide, plus réaliste**

Utiliser blocks validés :
```
File Source (.iq)
    ↓
Costas Loop (carrier sync)
    ↓
Symbol Sync (timing recovery)
    ↓
Custom Python Block (PRN despreading COSPAS-SARSAT)
    ↓
Binary Output → dec406_v2g.c
```

**Avantages** :
- Carrier/timing sync déjà validés
- Focus sur la partie spécifique (PRN despreading)
- Peut réutiliser analyse existante

**Désavantage** :
- Dépendance GNU Radio
- Moins standalone

### Option C : Aide Externe
**Chercher** :
- Expert SDR/DSP (forum, consultant)
- Implémentation open-source DSSS receiver
- Code SAR satellite similar
- Aide sur forums : GNU Radio, Reddit r/RTLSDR, etc.

### Option D : Approche Hybride
1. Générer signal TRÈS simplifié :
   - Pas de RRC filtering
   - Fréquence exactement 0 Hz
   - 1 sample/chip (pas de suréchantillonnage)
   - Phase exactement 0°

2. Démoduler ce signal simplifié → valider PRN despreading

3. Ajouter progressivement complexité :
   - RRC filter
   - Suréchantillonnage
   - Offset fréquence
   - Phase rotation

---

## 📝 Checklist Reprise

Avant de reprendre, vérifier :

- [ ] Compréhension théorique solide de :
  - [ ] DSSS spread spectrum (étalement/désétalement)
  - [ ] OQPSK (délai Tc/2, constellation)
  - [ ] Timing recovery (Gardner TED, loop filters)
  - [ ] Carrier recovery (Costas loop, PLL)
  - [ ] Phase ambiguity QPSK

- [ ] Outils de debug disponibles :
  - [ ] GNU Radio Companion (plots en temps réel)
  - [ ] MATLAB/Octave (prototypage)
  - [ ] Signal analyzer (spectre, constellation)

- [ ] Accès à :
  - [ ] Expert SDR/DSP (aide, review)
  - [ ] Code de référence fonctionnel
  - [ ] Documentation T.018 complète

- [ ] Temps disponible :
  - [ ] Minimum 2-3 jours continus
  - [ ] Pas de deadlines urgentes

---

## 🗂️ Organisation des Fichiers

```
~/Developpement/COSPAS-SARSAT/

├── ADALM-PLUTO/SARSAT_SGB/              ✅ Générateur (OPÉRATIONNEL)
│   ├── bin/sarsat_sgb                   → Exécutable
│   ├── test_known.iq                    → Frame test connue
│   └── test_sgb.iq                      → Frame test originale
│
├── balise_406MHz/dec406_v10.2/          ⏸️ Démodulateur (EN PAUSE)
│   ├── dsss_demod.h                     → API démodulateur
│   ├── dsss_demod.c                     → Implémentation (~800 lignes)
│   ├── main_iq.c                        → Entry point
│   ├── prn_generator.c/h                → PRN LFSR
│   ├── dec406_v2g.c                     ✅ Décodeur 2G (OPÉRATIONNEL)
│   ├── dec406_iq                        → Exécutable
│   ├── compare_bits.py                  → Analyse bit-à-bit
│   ├── test_all_phases.py               → Test transformations
│   ├── analyze_spectrum.py              → Analyse FFT
│   ├── ETAT_DES_LIEUX.md                → Vue d'ensemble projet
│   ├── ARCHITECTURE.html                → Architecture visuelle
│   ├── RESULTAT_DEMODULATEUR.md         → Résultats tests (obsolète)
│   └── ETAT_PAUSE_DEMODULATEUR.md       → Ce document
│
└── GNURADIO/gr-cospas/                  ⏸️ Tentatives GRC (non abouties)
```

---

## 💡 Leçons Apprises

### Ce qui a marché
1. ✅ Approche méthodique (découpage en étapes)
2. ✅ Tests avec frame connue (validation)
3. ✅ Scripts Python d'analyse (compare_bits)
4. ✅ Documentation continue

### Ce qui n'a pas marché
1. ❌ Implémentation "from scratch" trop ambitieuse
2. ❌ Sous-estimation de la complexité DSSS+OQPSK
3. ❌ Manque de validation étape par étape (plots, debug)
4. ❌ Pas de code de référence fonctionnel

### Recommandations
- Toujours valider avec plots à chaque étape
- Commencer par signal très simplifié
- Chercher code de référence avant d'implémenter
- Ne pas hésiter à utiliser blocks existants (GNU Radio)
- Consulter expert si blocage >1 jour

---

## 📞 Contacts Utiles (À Développer)

- Forums GNU Radio : https://discuss.gnuradio.org/
- Reddit r/RTLSDR : https://reddit.com/r/RTLSDR
- StackOverflow [dsp] tag
- LinkedIn (chercher experts "DSSS", "QPSK", "SDR")

---

## 🏁 Conclusion

**Décision sage de mettre en pause.**

Ce projet nécessite :
- Expertise DSP/SDR approfondie
- Temps conséquent (jours/semaines)
- Outils de validation appropriés
- Possiblement aide externe

**Le générateur TX et le décodeur RX fonctionnent.** Seul le démodulateur intermédiaire pose problème.

**Reprise recommandée avec** :
- Plus d'expérience en traitement du signal
- Accès à expert SDR
- Ou approche GNU Radio blocks

---

**Document créé le** : 2025-10-19
**Dernière mise à jour** : 2025-10-19
**Auteur** : Développement collaboratif
**Licence** : Creative Commons CC BY-NC-SA
