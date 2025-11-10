# Architecture dec406 - Spécifications Techniques

**Date**: 2025-11-10
**Version**: dec406_V10.2
**Objectif**: Démodulateur/Décodeur unifié COSPAS-SARSAT 406 MHz pour FGB (1G) et SGB (2G)

---

## 1. État actuel du projet

### Composants fonctionnels (✅ 100%)

| Composant | Status | Test | Fichier |
|-----------|--------|------|---------|
| Décodeur 1G (FGB) | ✅ 100% | dec406_hex testé | `src/dec406_v1g.c` |
| Décodeur 2G (SGB) | ✅ 100% | dec406_hex testé | `src/dec406_v2g.c` |
| Démodulateur BPSK | ✅ 100% | 2/2 fichiers validés | `gr-cospas C++` |
| Wrapper dispatch | ✅ 100% | Fonctionnel | `src/dec406.c` |

### Composant défaillant (❌)

| Composant | Status | Problème |
|-----------|--------|----------|
| Démodulateur DSSS OQPSK | ❌ **56% erreurs** | MATLAB Coder cassé (test_matlab_coder/) |

**Conclusion**: Tous décodeurs OK. Seul le démodulateur DSSS 2G est à réparer/remplacer.

---

## 2. Différences FGB vs SGB

### Caractéristiques techniques

| Paramètre | FGB (1G) | SGB (2G) |
|-----------|----------|----------|
| **Modulation** | BPSK | DSSS OQPSK |
| **Débit** | 400 bps | 300 bps |
| **Préambule** | 160ms porteuse non modulée | 50 bits à 0 modulés |
| **Durée burst** | ~520 ms | 1000 ms ±1ms |
| **Longueur trame** | 112 bits (court) ou 144 bits (long) | 250 bits (202 info + 48 BCH) |
| **Fréquence** | 406.025-406.040 MHz | 406.025-406.076 MHz |

### Répétition des bursts

**FGB**: ~50 secondes entre bursts

**SGB** (variable selon phase):
- **Phase initiale** (0-30s): 6 bursts espacés de **5s** (fixe)
- **Phase action** (30s-30min): 59 bursts espacés de **30s** (±5s randomisé)
- **Phase prolongée** (30min+): bursts espacés de **120s** (±5s randomisé)

**Cas particulier ELT DT**: 24 bursts à 5s, puis 18 bursts à 10s, puis 28.5s

---

## 3. Architecture complète - 5 étages

```
┌─────────────────────────────────────────────┐
│ 1. CAPTURE I/Q                              │
│    rtl_sdr -s 307200 -f $frq                │
│    → Buffer I/Q universel (identique FGB/SGB)|
└────────────────┬────────────────────────────┘
                 ↓ I/Q brut (float32)
┌─────────────────────────────────────────────┐
│ 2. WRAPPER #1 : dec406_iq (NOUVEAU)         │
│    - Lit I/Q depuis stdin                   │
│    - Double buffering (7.4 MB total)        │
│    - Appelle les 2 démodulateurs en //      │
└────────────────┬────────────────────────────┘
                 ↓ Buffer partagé
┌─────────────────────────────────────────────┐
│ 3. DÉMODULATION (DIFFÉRENTE)                │
│    ├─> BPSK : cherche 160ms porteuse pure   │
│    │    → I/Q → bits (112/144 bits)         │
│    └─> DSSS OQPSK : cherche préambule 50x0  │
│         → I/Q → despread → bits (250 bits)  │
└────────────────┬────────────────────────────┘
                 ↓ Bits bruts
┌─────────────────────────────────────────────┐
│ 4. WRAPPER #2 : decode_beacon() (EXISTANT)  │
│    - Switch selon longueur (112/144/250)    │
└────────┬────────────────────┬───────────────┘
         ↓ 112/144            ↓ 250
┌──────────────────┐  ┌──────────────────────┐
│ 5. DÉCODAGE      │  │                      │
│ (DIFFÉRENT)      │  │                      │
│ decode_1g()      │  │ decode_2g()          │
│ Parse proto 1G   │  │ Parse proto 2G + BCH │
└──────────────────┘  └──────────────────────┘
```

---

## 4. Architecture mémoire optimale - Double buffering

### Principe retenu : Architecture Direwolf

**Un seul processus, buffer partagé, deux démodulateurs parallèles**

```c
#define BUFFER_DURATION_MS 1500  // 1.5 secondes
#define SAMPLE_RATE 307200
#define BUFFER_SIZE (SAMPLE_RATE * BUFFER_DURATION_MS / 1000)

float buffer_A[BUFFER_SIZE * 2];  // I+Q : 3.7 MB
float buffer_B[BUFFER_SIZE * 2];  // I+Q : 3.7 MB
                                   // Total: 7.4 MB
```

### Schéma double buffering

```
┌─────────────────────────────────────────┐
│  Buffer A [█████████████████████]       │  ← rtl_sdr ÉCRIT ici
│  Buffer B [░░░░░░░░░░░░░░░░░░░░░]       │  ← Démodulateurs LISENT ici
└─────────────────────────────────────────┘

Quand Buffer A plein → SWAP

┌─────────────────────────────────────────┐
│  Buffer A [░░░░░░░░░░░░░░░░░░░░░]       │  ← Démodulateurs LISENT
│  Buffer B [█████████████████████]       │  ← rtl_sdr ÉCRIT
└─────────────────────────────────────────┘
```

### Avantages de cette architecture

1. **Zéro contention**: pas de conflit lecture/écriture
2. **Parallélisme**: Odroid multi-core utilisé efficacement
3. **Robustesse**: si démodulateur lent, pas de perte de données
4. **Mémoire**: 7.4 MB = 0.37% RAM sur Odroid C2 (2GB)
5. **Latence**: swap toutes les 1.5s (acceptable)
6. **Burst capture**: 1 burst SGB complet + marge 500ms

### Implémentation pseudo-code

```c
pthread_t writer_thread, demod_thread;
float *write_buf = buffer_A;
float *read_buf = buffer_B;

void* writer(void* arg) {
    while(running) {
        read(stdin, write_buf, BUFFER_SIZE);
        signal_buffer_ready();
        swap(&write_buf, &read_buf);
    }
}

void* demodulator(void* arg) {
    while(running) {
        wait_buffer_ready();

        // Les deux lisent le MÊME buffer
        bits_bpsk = try_demod_bpsk(read_buf);
        bits_dsss = try_demod_dsss(read_buf);

        if (bits_bpsk) decode_1g(bits_bpsk);
        if (bits_dsss) decode_2g(bits_dsss);
    }
}
```

---

## 5. Discrimination automatique FGB/SGB

**Pas de détection préalable nécessaire** - Chaque démodulateur rejette naturellement:

### Démodulateur BPSK (FGB)
1. Cherche **160ms de porteuse non modulée** (amplitude stable, phase constante)
2. Si trouvé → démodule BPSK 400 bps → 112/144 bits
3. Si non trouvé → **REJETTE** (pas du 1G)

### Démodulateur DSSS OQPSK (SGB)
1. Cherche **préambule DSSS** (50 bits à 0 modulés)
2. Si trouvé → despread OQPSK 300 bps → 250 bits
3. Si non trouvé → **REJETTE** (pas du 2G)

**Aucun risque de confusion**:
- BPSK ne verra JAMAIS 160ms de porteuse pure dans un burst SGB
- DSSS ne trouvera JAMAIS son préambule modulé dans porteuse FGB non modulée

---

## 6. Intégration avec scan406.pl

### Pipeline actuel (FGB seulement)

```perl
rtl_fm -M fm -s 12k -f $frq 2>/dev/null |
  sox -t raw -r 12k -e s -b 16 -c 1 - -t wav - $filter 2>/dev/null |
  dec406 --100 --M3 --une_minute 1>./trame 2>./code
```

**Problème**: `-M fm` ne fonctionne pas pour SGB OQPSK

### Pipeline futur (FGB + SGB)

```perl
rtl_sdr -s 307200 -f $frq 2>/dev/null |
  dec406_iq 1>./trame 2>./code
```

**dec406_iq** gère:
- Lecture I/Q stdin
- Double buffering
- Démodulation BPSK + DSSS en parallèle
- Appel decode_beacon() sur succès
- Sortie ./trame (affichage) et ./code (statut TROUVE/PAS)

---

## 7. Tâches restantes

### Priorité 1 : Réparer démodulateur DSSS OQPSK

**Problème**: MATLAB Coder produit 56% d'erreurs de bits

**Options**:
1. Corriger les 7 divergences dans l'implémentation manuelle C (archive/dsss_demod.c)
2. Porter un autre démodulateur DSSS fonctionnel
3. Débugger MATLAB Coder ligne par ligne

### Priorité 2 : Intégrer démodulateur BPSK C++

**Fichier**: `/home/fab2/Developpement/COSPAS-SARSAT/GNURADIO/gr-cospas/lib/cospas_sarsat_decoder_impl.cc`

**Options**:
1. Interfaçage C ↔ C++ dans dec406_iq
2. Porter C++ → C pur (552 lignes)
3. Wrapper Python/GNU Radio (dépendances)

### Priorité 3 : Créer dec406_iq

**Fonctionnalités**:
- Lecture I/Q depuis stdin
- Double buffering (7.4 MB)
- Appel démodulateurs en parallèle
- Interface avec decode_beacon()
- Sorties ./trame et ./code

### Priorité 4 : Adapter scan406.pl

**Modifications**:
- Remplacer `rtl_fm | sox` par `rtl_sdr`
- Appeler `dec406_iq` au lieu de `dec406`
- Vérifier timeout 56s compatible
- Tester avec balises CNES Toulouse

---

## 8. Contexte opérationnel

### Balises en activité

**Permanentes (étalonnage)**:
- 3 balises FGB CNES Toulouse (test système continu)

**Détresse (rares)**:
- Vraies balises FGB/SGB activées en urgence
- Probabilité négligeable de plusieurs simultanées

### Hardware cible

**Odroid C2/C4**:
- CPU: 4× Cortex-A53 (C2) / 4× Cortex-A55 (C4)
- RAM: 2 GB (C2) / 4 GB (C4)
- OS: Linux ARM64
- SDR: RTL-SDR dongle USB

### Bande de fréquence

**Scan**: 406.000 - 406.100 MHz (rtl_power)

**Canaux autorisés** (filtrage email selon T.012):
- 406.025 (B), 406.028 (C), 406.031 (D)
- 406.037 (F), 406.040 (G)
- 406.049 (J), 406.052 (K), 406.061 (N), 406.064 (O), 406.073 (R)
- 406.076 (S) - nouveau depuis 2025

---

## 9. Documentation de l'échec MATLAB Coder

**Fichier**: `MATLAB_CODER_FAILURE.md`

**Résumé**:
- 7 divergences identifiées entre C manuel et MATLAB Coder
- Test avec trame de référence: `89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4`
- **56% d'erreurs de bits** (28/50 bits incorrects)
- `BCH Errors: 0` est hardcodé (ligne 425: `*errs = 0;`), pas un vrai compteur
- Aucune correction BCH n'est effectuée

---

## 10. Structure du projet

```
dec406_V10.2/
├── src/              # Sources C essentiels
│   ├── dec406.c                 # Wrapper dispatch (existant)
│   ├── dec406_v1g.c            # Décodeur 1G (OK)
│   ├── dec406_v2g.c            # Décodeur 2G (OK)
│   ├── dec406_hex.c            # Test hex (OK)
│   └── dec406_iq.c             # NOUVEAU - À créer
├── include/          # Headers
│   └── dec406.h
├── build/            # Exécutables
├── data/             # Fichiers .iq, .bin, .txt
├── scripts/          # scan406.pl, etc.
├── tests/            # Tests unitaires
├── utils/            # Outils génération/analyse
├── archive/          # Ancienne implem DSSS
│   ├── dsss_demod.c
│   └── dsss_demod.h
├── test_matlab_coder/  # MATLAB Coder (CASSÉ)
├── Makefile
├── MATLAB_CODER_FAILURE.md
└── ARCHITECTURE_dec406.md  # Ce fichier
```

---

## Références

- COSPAS-SARSAT T.018 Rev. 12 (October 2024) - SGB specifications
- COSPAS-SARSAT T.012 Table H.2 - Frequency allocation
- gr-cospas: https://github.com/... (démodulateur BPSK C++)
- Direwolf: Architecture multi-modem référence

---

**Prochaine étape recommandée**: Réparer démodulateur DSSS OQPSK avant intégration dec406_iq
