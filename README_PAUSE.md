# Projet Démodulateur COSPAS-SARSAT 2G - README

**Date** : 2025-10-19
**Status** : ⏸️ **EN PAUSE TEMPORAIRE**

---

## ⚠️ ATTENTION

**Ce démodulateur n'est PAS opérationnel.**

Résultat actuel : **55.3% de bits corrects** (devrait être >95%)

---

## 📖 Documentation Complète

👉 **Consulter `ETAT_PAUSE_DEMODULATEUR.md`** pour :
- Analyse complète de l'échec
- Bugs identifiés
- Pistes pour reprise future
- Résultats tests détaillés

---

## 🎯 Ce Qui Fonctionne

### ✅ Générateur de Signal (SARSAT_SGB)
**Location** : `/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/`

```bash
cd /home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB
make
bin/sarsat_sgb -o test_output.iq
```

**Frame test connue** :
```
HEX: 89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
TAC: 9999, Serial: 13398, Country: 227 (France)
Position: 43.2°N, 5.4°E
```

### ✅ Décodeur de Frames (dec406_v2g.c)
**Location** : Ce dossier

```bash
./dec406_hex 89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
```

**Nécessite** : 300 bits corrects en entrée

---

## ❌ Ce Qui Ne Fonctionne PAS

### Démodulateur DSSS/OQPSK (dec406_iq)

```bash
./dec406_iq test_known.iq
# Résultat: 55% bits corrects ❌
```

**Problèmes** :
- Timing recovery incorrect
- Phase ambiguity mal résolue
- DSSS despreading SNR très bas
- Costas loop divergente (désactivée)

---

## 🛠️ Compilation

```bash
gcc -o dec406_iq main_iq.c dsss_demod.c prn_generator.c \
    dec406.c dec406_v1g.c dec406_v2g.c display_utils.c -lm -O2
```

---

## 📁 Fichiers Importants

| Fichier | Description | Status |
|---------|-------------|--------|
| `ETAT_PAUSE_DEMODULATEUR.md` | **Documentation complète** | ⭐ À LIRE |
| `dsss_demod.c` | Démodulateur (~800 lignes) | ⏸️ Non fonctionnel |
| `main_iq.c` | Entry point IQ | ⏸️ Non fonctionnel |
| `dec406_v2g.c` | Décodeur 2G | ✅ Opérationnel |
| `compare_bits.py` | Analyse bit-à-bit | 🔧 Outil debug |
| `test_all_phases.py` | Test transformations | 🔧 Outil debug |

---

## 🔄 Reprise Future

**Avant de reprendre**, consulter `ETAT_PAUSE_DEMODULATEUR.md` section "Pistes pour Reprise Future"

**Options** :
- A) Debug approfondi (long, incertain)
- B) GNU Radio blocks (plus rapide)
- C) Aide externe / code existant
- D) Simplification radicale

**Pré-requis** :
- Expertise DSP/SDR avancée
- Temps disponible (jours/semaines)
- Outils de validation (plots, analyseur spectre)

---

## 📞 Contact / Aide

- Forums GNU Radio : https://discuss.gnuradio.org/
- Reddit r/RTLSDR
- StackOverflow [dsp] tag

---

## ✅ Workflow Opérationnel Actuel

**Génération + Décodage Direct** (sans démodulation IQ) :

```bash
# 1. Générer frame
cd /home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB
bin/sarsat_sgb -o test.iq
# Note frame hex affichée dans output

# 2. Décoder frame directement (hex → décodage)
cd /home/fab2/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_v10.2
./dec406_hex <HEX_DE_LA_FRAME>
```

**Gap** : IQ → bits (démodulateur non fonctionnel)

---

**Projet mis en pause - 2025-10-19**
**Reprise recommandée avec expertise DSP appropriée**
