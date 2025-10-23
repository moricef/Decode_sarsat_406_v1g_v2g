# État des Lieux - Système Complet COSPAS-SARSAT 2G

**Date**: 2025-10-19
**Objectif**: Réception complète IQ → Décodage de balises 406 MHz 2G

---

## 📊 Architecture Complète du Système

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         SIGNAL IQ SOURCE                                 │
│  • RTL-SDR @ 403 MHz (Nooelec Smartee V5)                              │
│  • Fichier IQ enregistré (.iq, .wav)                                   │
│  • PlutoSDR @ 403 MHz                                                   │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                   DÉMODULATEUR DSSS/OQPSK                               │
│  ❌ MANQUANT - À IMPLÉMENTER                                            │
│                                                                          │
│  Étapes nécessaires:                                                    │
│  1. AGC (Automatic Gain Control)                                        │
│  2. Détection préambule (50 bits alternés)                             │
│  3. Correction fréquence grossière (Doppler ±12 kHz)                   │
│  4. Correction fréquence fine (PLL)                                     │
│  5. Récupération timing (Symbol Sync)                                   │
│  6. Résolution ambiguïté de phase (4 rotations possibles)              │
│  7. Démodulation QPSK → chips I/Q                                       │
│  8. DÉSÉTALEMENT DSSS:                                                  │
│     - Génération PRN I/Q (LFSR x²³+x¹⁸+1)                              │
│     - Corrélation XOR sur 256 chips/bit                                │
│     - Décision vote majoritaire (>128/256)                             │
│     - Entrelacement I/Q → flux bits (odd→I, even→Q)                    │
│  9. Output: 300 bits (50 préambule + 250 payload)                      │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼ 300 bits démodulés
┌─────────────────────────────────────────────────────────────────────────┐
│                   DÉCODEUR DE FRAMES 2G                                 │
│  ✅ EXISTANT - dec406_v2g.c (Production-ready)                          │
│                                                                          │
│  Fonctionnalités:                                                       │
│  • BCH(250,202) error correction (T.018 Appendix B)                    │
│  • Extraction 23-HEX ID, TAC, Serial                                   │
│  • Décodage Country Code (MID database)                                │
│  • Décodage GPS Position (3.4m resolution)                             │
│  • Décodage Vessel ID (MMSI, Call Sign, ICAO24)                        │
│  • Support tous Rotating Fields (RF#0-15)                              │
│  • RLS Type 1/2/3, G.008 Objective Requirements                        │
│  • Génération lien OpenStreetMap                                       │
└────────────────────────────┬────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         DECODED OUTPUT                                   │
│  • Console display (formatted)                                          │
│  • Email notification (via Perl script)                                │
│  • OSM map link                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## ✅ Ce qui EXISTE (Fonctionnel)

### 1. Générateur de Signal 2G (`SARSAT_SGB/`)
**Localisation**: `~/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/`

**Status**: ✅ **CONFORME T.018** (après correction architecture OQPSK)

**Fonctionnalités**:
- ✅ Génération trame T.018 (252 bits: 2 header + 202 data + 48 BCH)
- ✅ Encodage BCH(250,202)
- ✅ PRN sequences (LFSR x²³+x¹⁸+1, init: 0x000001 / 0x000041)
- ✅ DSSS spreading (256 chips/bit, 38.4 kchips/s)
- ✅ **OQPSK correct**: bits impairs→I (150 bits), bits pairs→Q (150 bits)
- ✅ RRC pulse shaping (α=0.5)
- ✅ Transmission PlutoSDR @ 403 MHz
- ✅ Génération fichier IQ (2.5M samples @ 2.5 MHz = 1.0 seconde)

**Commit récent**: `e1eab57` - "Fix T.018 OQPSK: Implement correct odd/even bit separation"

**Fichier test validé**: `test_sgb.iq` (2,499,999 samples, 1.0s @ 300 bps)

---

### 2. Décodeur de Frames 2G (`dec406_v10.2/`)
**Localisation**: `~/Developpement/COSPAS-SARSAT/balise_406MHz/dec406_v10.2/`

**Status**: ✅ **Production-ready**

**Architecture**:
```c
// Entrée: 250 bits (202 data + 48 BCH)
bch_decode_250_202(msg, decoded_data);  // T.018 Appendix B
decode_2g_frame(decoded_data);           // Parse tous les champs
```

**Validations**:
- ✅ BCH syndrome = 0 sur frames hardware dsPIC33CK
- ✅ Frames générées par `SARSAT_SGB` décodées correctement
- ✅ Conforme T.018 Rev.12

**Exemples de frames testées**:
```
Frame 1: 89C3F45639195999A02B33326C3EC4400007FFF00C0283200000DCA2C07A361
  ✓ BCH valid, France, EPIRB, 43.20°N 5.40°E

Frame 2: 0C0E7456390956CCD02799A2468ACF135787FFF00C02832000037707609BC0F
  ✓ BCH valid, France, EPIRB, 42.85°N 4.95°E
```

---

## ❌ Ce qui MANQUE (À Implémenter)

### Démodulateur DSSS/OQPSK : IQ samples → 300 bits

**Gap actuel**:
```
Signal IQ (2.5 MHz)  →  [❌ MANQUANT]  →  300 bits démodulés  →  ✅ dec406_v2g.c
```

**Besoin**:
1. **Détection burst** (preamble correlation)
2. **Synchronisation** (frequency, phase, timing recovery)
3. **Désétalement DSSS** (PRN correlation, 256 chips/bit)
4. **OQPSK demod** (chips → bits avec odd/even separation)

---

## 📚 Références Disponibles

### Documentation Correcte ✅
1. **T.018 Rev.12** (spec officielle)
   - `/MPLABXProjects/SARSAT_T018_dsPIC33CK.X/Docs/Docs_COSPAS-SARSAT/2024/`
   - Section 2.2.3.b: Architecture OQPSK (odd/even bits)
   - Appendix D: PRN generation (LFSR)
   - Appendix B: BCH(250,202)

2. **PDF MATLAB officiel** (MathWorks R2024a)
   - `/Docs/Matlab DSSS/DSSSReceiverForSARbasedTrackingSystem.pdf`
   - ✅ Architecture complète du récepteur
   - ✅ PRN generation correcte
   - ✅ Preamble detection avec frequency search
   - ✅ Coarse/Fine frequency correction
   - ✅ Timing recovery (Gardner TED)
   - ✅ Phase ambiguity resolution
   - ✅ DSSS despreading (XOR correlation)

### Code de Référence Incorrect ❌
**NE PAS UTILISER**:
- `/DSSS_Complete/matlab_code/*.m` (erreurs LFSR, init states, architecture)

---

## 🎯 Prochaines Étapes

### Phase 1: Implémentation Démodulateur GNU Radio

**Option A: Bloc Python Custom** (recommandé pour prototype)
```python
# Bloc GRC Python personnalisé
class oqpsk_dsss_demod(gr.sync_block):
    def __init__(self):
        # Init PRN generators (T.018 compliant)
        # Init sync loops (Costas, Symbol Sync)
        # Init preamble detector
        pass

    def work(self, input_items, output_items):
        # 1. Preamble detection
        # 2. Freq/Phase sync
        # 3. Timing recovery
        # 4. DSSS despreading
        # 5. Output 300 bits
        pass
```

**Option B: Module GNU Radio OOT** (production)
```bash
gr_modtool newmod cospas
gr_modtool add -t sync oqpsk_dsss_demod
# Implémenter en C++ avec notre code C validé
```

### Phase 2: Intégration avec Décodeur Existant

```bash
# Pipeline complet
gnuradio-companion flowgraph.grc  # IQ → 300 bits
    ↓
python bridge_script.py           # Bits → Hex
    ↓
./dec406_hex <hex_string>         # Décodage complet
```

### Phase 3: Tests End-to-End

1. ✅ Signal générateur: `SARSAT_SGB/test_sgb.iq`
2. ❌ Démodulateur GRC: à implémenter
3. ✅ Décodeur: `dec406_v2g.c`
4. ✅ Validation: comparer avec frame originale

---

## 📝 Décisions Techniques

### Choix Architectural

| Aspect | Décision | Justification |
|--------|----------|---------------|
| **PRN Generation** | LFSR x²³+x¹⁸+1 | T.018 Appendix D |
| **Initial States** | I: 0x000001, Q: 0x000041 | T.018 Table 2.2 |
| **Spreading** | 256 chips/bit | T.018 Section 2.2.3 |
| **OQPSK Split** | Odd→I, Even→Q | T.018 Section 2.2.3.b |
| **Sample Rate** | 2.5 MHz | Validé avec PlutoSDR |
| **Chip Rate** | 38.4 kchips/s | T.018 spec |
| **Despreading** | XOR + vote majoritaire | MATLAB PDF page 14 |
| **BCH** | Utiliser dec406_v2g.c | Déjà validé T.018 |

---

## 🔗 Liens Utiles

- **Générateur signal**: `/ADALM-PLUTO/SARSAT_SGB/`
- **Décodeur frames**: `/balise_406MHz/dec406_v10.2/`
- **Flowgraph GRC**: `/gnuradio/OQPSK_NewGn_orig.grc`
- **Doc T.018**: `/MPLABXProjects/.../Docs_COSPAS-SARSAT/`
- **PDF MATLAB**: `/Docs/Matlab DSSS/DSSSReceiverForSARbasedTrackingSystem.pdf`

---

## 📊 Synthèse

| Composant | Status | Priorité |
|-----------|--------|----------|
| Générateur TX | ✅ OK | - |
| Fichier IQ test | ✅ OK | - |
| Démodulateur | ❌ Manquant | 🔴 HAUTE |
| Décodeur frames | ✅ OK | - |
| Tests E2E | ❌ Bloqué | 🔴 HAUTE |

**Bloqueur critique**: Démodulateur DSSS/OQPSK pour GNU Radio

**Action immédiate**: Implémenter bloc Python GRC basé sur PDF MATLAB officiel
