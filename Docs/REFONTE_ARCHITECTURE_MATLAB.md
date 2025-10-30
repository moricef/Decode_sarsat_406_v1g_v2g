# Refonte Architecture dsss_demod.c - Alignement sur MATLAB

**Date:** 2025-10-30
**Statut:** PROPOSITION DE REFONTE
**Référence:** `Docs/Matlab DSSS/DSSSReceiverForSARbasedTrackingSystem.pdf`

## 1. PROBLÈME IDENTIFIÉ

Le démodulateur `dsss_demod.c` a une architecture incorrecte qui cause:
- **Timing recovery défaillant** (perte info délai Tc/2)
- **Phase ambiguity resolution inefficace** (test sur chips étalés au lieu de bits)
- **Corrélation despreading faible** (2.5-3.2% au lieu de >70%)
- **Taux de bits corrects: 55.3%** au lieu de >95%

### Erreurs architecturales principales

```
NOTRE CODE (INCORRECT):
OQPSK → [CONVERT QPSK] → [TIMING RECOVERY] → [PHASE TEST] → [DESPREAD]
        ligne 2312      ligne 2346           ligne 1224      ligne 1533

MATLAB (CORRECT):
OQPSK → [TIMING RECOVERY avec OQPSK] → [DESPREAD] → [PHASE TEST] → BITS
        page 12                         page 14      page 13-14
```

## 2. ARCHITECTURE MATLAB - SPÉCIFICATION COMPLÈTE

### 2.1 Flux de traitement (pages 11-14)

```
┌─────────────────────────────────────────────────────────────┐
│ INPUT: carrierSyncOut (OQPSK, suréchantillonné)           │
│ - Après coarse + fine frequency correction                 │
│ - Format: float complex, sps=8 samples/chip                │
│ - Longueur: ~38400 chips × 8 = 307200 samples             │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ÉTAPE 1: PATH DETECTION (page 11)                          │
│ ────────────────────────────────────────────────────────── │
│ Objectif: Localiser précisément le début du préambule      │
│                                                             │
│ 1.1 Conversion TEMPORAIRE OQPSK→QPSK (pour corrélation)   │
│     carrierSyncOutQPSK = [                                 │
│         real(carrierSyncOut(1:end-sps/2)) +                │
│         1i*imag(carrierSyncOut(sps/2+1:end));              │
│         zeros(sps/2,1)                                      │
│     ]                                                       │
│                                                             │
│ 1.2 Corrélation avec préambule QPSK complet               │
│     preamble0 = preambleQPSK(preambleOffset+1:end)         │
│     [FSPSampIdx, corrBuffer2] =                            │
│         helperPolyphaseCorrelator(...)                     │
│                                                             │
│ OUTPUT: FSPSampIdx (index de début du signal utile)       │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ÉTAPE 2: TIMING RECOVERY ★★★ CRITIQUE ★★★ (page 12)      │
│ ────────────────────────────────────────────────────────── │
│ Objectif: Récupérer les symboles à Ts (chip rate)          │
│                                                             │
│ 2.1 Configuration SymbolSynchronizer                       │
│     symbolSynchronizer = comm.SymbolSynchronizer(          │
│         Modulation = 'OQPSK',          ← OQPSK, pas QPSK! │
│         NormalizedLoopBandwidth = 0.001,                   │
│         DetectorGain = 4,                                   │
│         DampingFactor = 2,                                  │
│         SamplesPerSymbol = sps                             │
│     )                                                       │
│                                                             │
│ 2.2 Timing recovery                                        │
│     INPUT:  carrierSyncOut(FSPSampIdx:end)  ← Signal OQPSK│
│     OUTPUT: [syncedQPSK, timingError]                      │
│                                                             │
│     syncedQPSK contient:                                   │
│     - 38400 symboles QPSK (1 symbole = 1 chip)            │
│     - Déjà compensé pour le délai Tc/2                    │
│     - Conversion OQPSK→QPSK faite PENDANT la synchro      │
│                                                             │
│ IMPORTANT:                                                  │
│ - Le SymbolSynchronizer avec Modulation='OQPSK':          │
│   1) Retarde la composante Q de Tc/2                       │
│   2) Fait la synchronisation temporelle                    │
│   3) Convertit en QPSK                                     │
│ - Tout cela en UNE SEULE opération intégrée               │
│                                                             │
│ OUTPUT: syncedQPSK (38400 symboles QPSK)                   │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ÉTAPE 3: DEMODULATION + PHASE AMBIGUITY (page 13-14)      │
│ ────────────────────────────────────────────────────────── │
│ Objectif: Résoudre rotation de phase et swap I/Q           │
│                                                             │
│ 3.1 Boucle sur toutes les combinaisons possibles          │
│     for p = 0:1              ← swap I/Q ou non             │
│         for k = 0:3          ← rotation 0°, 90°, 180°, 270°│
│                                                             │
│ 3.2 Pour chaque combinaison (p, k):                       │
│                                                             │
│     a) Rotation de phase                                   │
│        rxSig = pskdemod(                                   │
│            syncedQPSK * exp(1i*k*pi/2),  ← rotation       │
│            4, pi/4, OutputType='bit'                       │
│        )                                                    │
│                                                             │
│     b) Extraction I et Q                                   │
│        rx.Ci = rxSig(1:2:numChips*2)    ← bits impairs    │
│        rx.Cq = rxSig(2:2:numChips*2)    ← bits pairs      │
│                                                             │
│     c) DESPREADING du préambule ★ CLEF ★                  │
│        Ibdn = xor(rx.Ci(1+p:3300+p), DSSS.PRN_I(1:3300))  │
│        Qbdn = xor(rx.Cq(1:3300), DSSS.PRN_Q(1:3300))      │
│        preambleTest = reshape([Ibdn Qbdn]', [], 1) - 0.5  │
│                                                             │
│     d) Corrélation avec préambule CONNU (all zeros)       │
│        preambleSymbol = preambleChips - 0.5  (all -0.5)   │
│        preambleDetector.Preamble = preambleSymbol         │
│        [pIdx, pMet] = preambleDetector(preambleTest)      │
│                                                             │
│     e) Si corrélation forte → on a trouvé la bonne phase! │
│        if ~isempty(pIdx)                                   │
│            break  ← Sortie des boucles                     │
│                                                             │
│ 3.3 Extraction finale avec les bons paramètres (p, k)     │
│     rx.Ci = rxSig(sIdxI:2:sIdxI+2*numChips-1)             │
│     rx.Cq = rxSig(sIdxQ:2:sIdxQ+2*numChips-1)             │
│                                                             │
│ OUTPUT: rx.Ci, rx.Cq (chips déspreads, 38400 chacun)      │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ÉTAPE 4: DESPREADING FINAL (page 14)                       │
│ ────────────────────────────────────────────────────────── │
│ Objectif: Récupérer les 300 bits (150 I + 150 Q)          │
│                                                             │
│ 4.1 Corrélation avec PRN                                   │
│     Ibdn = xor(rx.Ci, DSSS.PRN_I)                         │
│     Qbdn = xor(rx.Cq, DSSS.PRN_Q)                         │
│                                                             │
│ 4.2 Reshape en matrice [256 × 150]                        │
│     Ichips = reshape(Ibdn, 256, 150)                       │
│     Qchips = reshape(Qbdn, 256, 150)                       │
│                                                             │
│ 4.3 ML decoding (majority vote)                            │
│     threshold = 256/2 = 128                                │
│     Ibits = sum(Ichips, 1) > threshold                     │
│     Qbits = sum(Qchips, 1) > threshold                     │
│                                                             │
│ 4.4 Multiplexage I/Q                                       │
│     despreadMessage = reshape([Ibits; Qbits], [], 1)       │
│                                                             │
│ OUTPUT: despreadMessage (300 bits)                         │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ ÉTAPE 5: BCH DECODE (page 15)                              │
│ ────────────────────────────────────────────────────────── │
│ hDecode = comm.BCHDecoder(255, 207, poly)                  │
│ [rxPayload, errs] = hDecode(                               │
│     despreadMessage(preambleLength+1:packetLength)         │
│ )                                                           │
│                                                             │
│ OUTPUT: rxPayload (202 bits corrigés)                      │
└─────────────────────────────────────────────────────────────┘
```

## 3. COMPARAISON DÉTAILLÉE: MATLAB vs NOTRE CODE

### 3.1 Timing Recovery

| Aspect | MATLAB | dsss_demod.c | Impact |
|--------|--------|--------------|--------|
| **Input** | OQPSK (carrierSyncOut) | QPSK (après oqpsk_to_qpsk) | ❌ Perte délai Tc/2 |
| **Modulation** | 'OQPSK' | Générique Gardner | ❌ Pas de gestion OQPSK |
| **Conversion** | PENDANT synchro | AVANT synchro (ligne 2312) | ❌ Ordre incorrect |
| **Output** | 38400 symboles QPSK | Confusion chips/symboles | ❌ Terminologie |
| **Délai Tc/2** | Géré automatiquement | Perdu après conversion | ❌ CRITIQUE |

### 3.2 Phase Ambiguity Resolution

| Aspect | MATLAB | dsss_demod.c | Impact |
|--------|--------|--------------|--------|
| **Quand?** | APRÈS timing recovery | AVANT timing recovery | ❌ Ordre incorrect |
| **Test sur quoi?** | Bits déspreads | Chips étalés | ❌ Pas de signal clair |
| **Méthode test** | Despread + corrélation préambule | Distribution quadrants | ❌ Inefficace |
| **Combinaisons** | 8 (4 rot × 2 swap) | 8 (4 rot × 2 swap) | ✅ OK |
| **Corrélation** | >70% si bon | 25% uniforme | ❌ Pas de discrimination |

### 3.3 Despreading

| Aspect | MATLAB | dsss_demod.c | Impact |
|--------|--------|--------------|--------|
| **Input** | Bits démodulés (0/1) | Chips complexes | ❌ Type incorrect |
| **PRN** | Bits (0/1) | Chips (-1/+1) | ⚠️ Convention différente |
| **XOR** | xor(bits, PRN) | Corrélation complexe | ⚠️ OK mais différent |
| **Reshape** | [256 × 150] | [256 × 150] | ✅ OK |
| **ML decode** | sum > 128 | sum > 128 | ✅ OK |

## 4. PLAN DE REFONTE

### 4.1 Modifications requises (par ordre de priorité)

#### PRIORITÉ 1: Refactoriser timing recovery pour OQPSK

**Fichier:** `dsss_demod.c` lignes 2312-2346

**Changement:**
```c
// AVANT (incorrect):
oqpsk_to_qpsk(fine_sync_out, qpsk_out, burst_length, sps_main);
dsss_timing_recovery_corrected(&qpsk_out[1], symbols, ...);

// APRÈS (correct, style MATLAB):
dsss_timing_recovery_oqpsk(fine_sync_out, symbols_qpsk, ...);
// Note: fine_sync_out est OQPSK
// Note: symbols_qpsk est la sortie QPSK (conversion intégrée)
```

**Nouvelle fonction requise:**
```c
/**
 * Timing recovery pour signal OQPSK
 * Équivalent de comm.SymbolSynchronizer avec Modulation='OQPSK'
 *
 * Fonctionnement:
 * 1. Retarde la composante Q de Tc/2
 * 2. Applique Gardner TED sur signal retardé
 * 3. Produit des symboles QPSK (1 symbole/chip)
 *
 * @param oqpsk_in   Signal OQPSK suréchantillonné (input)
 * @param qpsk_out   Symboles QPSK à chip rate (output)
 * @param length     Nombre de samples input
 * @param sps        Samples per symbol
 * @param state      État du timing recovery
 * @return           Nombre de symboles QPSK produits
 */
int dsss_timing_recovery_oqpsk(
    float complex *oqpsk_in,
    float complex *qpsk_out,
    int length,
    int sps,
    timing_recovery_state_t *state
);
```

#### PRIORITÉ 2: Déplacer phase ambiguity APRÈS despreading

**Fichier:** `dsss_demod.c` lignes 1224-1458

**Changement:**
```c
// ANCIEN ORDRE (incorrect):
// 1. Timing recovery
// 2. Phase ambiguity test (ligne 1224)
// 3. Despreading (ligne 1533)

// NOUVEL ORDRE (correct):
// 1. Timing recovery OQPSK → symboles QPSK
// 2. Démodulation QPSK → chips I/Q
// 3. Pour chaque rotation (0°, 90°, 180°, 270°):
//    Pour chaque swap (I/Q normal, I/Q swapped):
//      a) Despread préambule avec PRN
//      b) Corrèle avec préambule connu (all zeros)
//      c) Mesure métrique de corrélation
// 4. Garde la meilleure combinaison (rotation, swap)
// 5. Despreading final du message complet
```

**Nouvelle fonction requise:**
```c
/**
 * Résolution d'ambiguïté de phase par despreading du préambule
 * Équivalent de la boucle MATLAB pages 13-14
 *
 * @param qpsk_symbols  Symboles QPSK à chip rate (38400)
 * @param prn_i         Séquence PRN canal I
 * @param prn_q         Séquence PRN canal Q
 * @param best_rotation [OUT] Rotation optimale (0, 1, 2, 3)
 * @param best_swap     [OUT] Swap optimal (0=normal, 1=swap)
 * @return              Métrique de corrélation de la meilleure config
 */
float dsss_resolve_phase_by_despread(
    float complex *qpsk_symbols,
    int num_symbols,
    int8_t *prn_i,
    int8_t *prn_q,
    int *best_rotation,
    int *best_swap
);
```

#### PRIORITÉ 3: Simplifier despreading

**Fichier:** `dsss_demod.c` lignes 1533-1650

**Changement:**
```c
// Utiliser la rotation et swap déterminés à l'étape précédente
// Appliquer XOR avec PRN (pas corrélation complexe)
// ML decode par majority vote sur chaque colonne

// Style MATLAB:
for (int bit = 0; bit < 150; bit++) {
    int sum_i = 0, sum_q = 0;
    for (int chip = 0; chip < 256; chip++) {
        int idx = bit * 256 + chip;
        sum_i += (chips_i[idx] == prn_i[idx]) ? 1 : 0;
        sum_q += (chips_q[idx] == prn_q[idx]) ? 1 : 0;
    }
    bits_i[bit] = (sum_i > 128) ? 0 : 1;  // Majority vote
    bits_q[bit] = (sum_q > 128) ? 0 : 1;
}
```

### 4.2 Ordre d'implémentation

```
PHASE 1: Timing Recovery OQPSK
├── 1.1 Créer dsss_timing_recovery_oqpsk()
├── 1.2 Tester sur test_signal_CORRECT_FIXED.iq
├── 1.3 Vérifier: 38400 symboles QPSK produits
└── 1.4 Vérifier: constellation QPSK alignée

PHASE 2: Phase Ambiguity par Despreading
├── 2.1 Créer dsss_resolve_phase_by_despread()
├── 2.2 Implémenter boucle 8 combinaisons
├── 2.3 Pour chaque: despread préambule + corrélation
├── 2.4 Tester: métrique >70% pour bonne config
└── 2.5 Tester: métrique <30% pour mauvaises configs

PHASE 3: Intégration
├── 3.1 Modifier main processing loop
├── 3.2 Nouveau flux: timing_recovery_oqpsk → resolve_phase → despread
├── 3.3 Supprimer ancien oqpsk_to_qpsk (ligne 2312)
├── 3.4 Supprimer ancien resolve_phase_ambiguity (ligne 1224)
└── 3.5 Tester sur signal complet

PHASE 4: Validation
├── 4.1 Vérifier BER < 5% (objectif: 0%)
├── 4.2 Vérifier corrélation despreading > 70%
├── 4.3 Tester avec différents SNR
└── 4.4 Documenter résultats
```

### 4.3 Critères de succès

| Métrique | Valeur actuelle | Valeur cible | Comment mesurer |
|----------|----------------|--------------|-----------------|
| Timing recovery | 99.8% chips | 100% symboles | Nb symboles = 38400 |
| Corrélation préambule | 58.0% | >90% | Phase 1 despreading |
| Corrélation despreading | 2.5-3.2% | >70% | Despreading final |
| Quadrant distribution | 25%/25%/25%/25% | >70% un quadrant | Test phase ambiguity |
| BER (Bit Error Rate) | 44.7% | <5% (objectif 0%) | Bits décodés vs expected |
| BCH errors | N/A | 0-6 errors | Sortie BCH decoder |

## 5. RISQUES ET MITIGATIONS

### 5.1 Risques techniques

| Risque | Probabilité | Impact | Mitigation |
|--------|------------|--------|------------|
| Gardner TED incompatible avec OQPSK | Moyenne | Élevé | Implémenter TED spécifique OQPSK ou utiliser algorithme MATLAB |
| Corrélation préambule plus complexe | Faible | Moyen | Suivre exactement l'implémentation MATLAB page 13 |
| Délai Tc/2 mal géré | Moyenne | Élevé | Valider avec analyse temporelle détaillée |
| Conventions PRN différentes | Faible | Faible | Vérifier mapping 0/1 vs -1/+1 |

### 5.2 Validation incrémentale

Après chaque phase, valider avec:
1. **Test signal synthétique** (`test_signal_CORRECT_FIXED.iq`)
2. **Fichier bits attendus** (`expected_bits_FIXED.bin`)
3. **Mesures intermédiaires** (corrélations, distributions)
4. **Logs détaillés** pour debug

## 6. RÉFÉRENCES

### 6.1 Documents

- **MATLAB DSSS Receiver:** `Docs/Matlab DSSS/DSSSReceiverForSARbasedTrackingSystem.pdf`
  - Page 11: Path Detection
  - Page 12: **Timing Recovery OQPSK** ★★★
  - Pages 13-14: **Phase Ambiguity Resolution** ★★★
  - Page 14: Despreading
  - Page 15: BCH Decode

- **COSPAS-SARSAT T.018:** Spécification 2G beacons
  - §2.2.4: Préambule ALL ZERO
  - §4.3: Séquences PRN

### 6.2 Code MATLAB clé

**Timing Recovery (page 12, lignes 1-8):**
```matlab
symbolSynchronizer = comm.SymbolSynchronizer( ...
    Modulation = 'OQPSK', ...
    NormalizedLoopBandwidth = 0.001, ...
    DetectorGain = 4, ...
    DampingFactor = 2, ...
    SamplesPerSymbol = sps);
[syncedQPSK,timingError] = symbolSynchronizer(carrierSyncOut(FSPSampIdx:end));
```

**Phase Ambiguity (page 13-14, lignes 1-35):**
```matlab
for p=0:1
    for k=0:3
        rxSig = pskdemod(syncedQPSK*exp(1i*k*pi/2),4,pi/4,"OutputType","bit");
        rx.Ci = [rxSig(1:2:numChips*2);0];
        rx.Cq = [rxSig(2:2:numChips*2);0];

        preambleTest = reshape([rx.Ci(1+p:3300+p) rx.Cq(1:3300)]',[],1)-0.5;
        [pIdx,pMet] = preambleDetector(preambleTest);

        if ~isempty(pIdx)
            break;
        end
    end
    if ~isempty(pIdx)
        break;
    end
end
```

**Despreading (page 14, lignes 1-14):**
```matlab
Ibdn = (xor(rx.Ci, DSSS.PRN_I));
Qbdn = (xor(rx.Cq, DSSS.PRN_Q));
Ichips = reshape(Ibdn,system.spreadingFactor,[]);
Qchips = reshape(Qbdn,system.spreadingFactor,[]);

threshold = system.spreadingFactor/2;
Ibits = sum(Ichips,1) > threshold;
Qbits = sum(Qchips,1) > threshold;

despreadMessage = reshape([Ibits;Qbits],[],1);
```

## 7. PROCHAINES ÉTAPES

1. ✅ **Documenter l'architecture MATLAB** (ce document)
2. ⏳ **Review et validation du plan** avec utilisateur
3. ⏳ **PHASE 1:** Implémenter `dsss_timing_recovery_oqpsk()`
4. ⏳ **PHASE 2:** Implémenter `dsss_resolve_phase_by_despread()`
5. ⏳ **PHASE 3:** Intégration dans `dsss_demod.c`
6. ⏳ **PHASE 4:** Tests et validation complète

---

**Note importante:** Cette refonte corrige les 3 erreurs architecturales majeures identifiées:
1. ❌ Conversion OQPSK→QPSK trop tôt (avant timing recovery)
2. ❌ Phase ambiguity resolution sur chips étalés (avant despreading)
3. ❌ Timing recovery générique (pas de gestion OQPSK)

L'architecture MATLAB est **validée et fonctionnelle**. Il suffit de la reproduire fidèlement.
