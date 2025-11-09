# Migration vers MATLAB Coder - DSSS OQPSK Receiver

**Date:** 2025-11-09
**Objectif:** Remplacer l'implémentation manuelle C par le code généré automatiquement depuis MATLAB pour garantir 100% de compatibilité avec la référence MATLAB.

---

## Contexte

L'implémentation manuelle du démodulateur DSSS OQPSK (`dsss_demod.c`, 1305 lignes) présentait **7 divergences majeures** avec la référence MATLAB:

### Divergences Critiques Identifiées

1. **Carrier Synchronizer:** Ne traitait que 1/8 des échantillons (condition `i % sps == 0`) au lieu de TOUS les échantillons comme MATLAB
2. **Corrélation:** Utilisait FFT au lieu de corrélation temporelle directe
3. **Détecteur erreur phase:** Algorithme différent (échantillon i-sps/2 vs i-1)
4. **Architecture:** 1 fichier (1305 lignes) vs 29 fichiers (2600+ lignes) = "simplification outragère"
5. **Précision:** Float simple vs Double précision MATLAB
6. **Fonctions math:** Standard C vs fonctions MATLAB optimisées (`rt_hypotd_snf`, `rt_atan2d_snf`)
7. **Gestion mémoire:** Buffers statiques vs arrays dynamiques (emxArray)

### Résultats Avant Migration

- **Notre code:** `ERROR: Preamble not detected` (corrélation max 0.28 < seuil 0.35)
- **MATLAB Coder:** `SUCCESS, 0 erreurs, 202 bits décodés`

---

## Solution Adoptée: OPTION B - Code MATLAB Coder

Au lieu de corriger manuellement les 7 divergences (risque élevé d'en introduire d'autres), **adoption du code MATLAB Coder** comme implémentation de référence.

### Avantages

✅ **Garanti 100% identique** à MATLAB (généré automatiquement)
✅ **Déjà testé et validé** (SUCCESS sur fichier test)
✅ **Maintenance simplifiée** (régénération depuis MATLAB possible)
✅ **Aucun risque** de divergence algorithmique

### Structure Adoptée

```
dec406_V10.2/
├── dsss_demod.h                    # API publique (nouvelle version)
├── dsss_demod_matlab.c             # Adapter entre notre API et MATLAB
├── test_matlab_coder/              # Code MATLAB Coder (29 fichiers)
│   ├── dsss_receiver.c             # Fonction principale MATLAB
│   ├── dsss_receiver.h
│   ├── dsss_receiver_types.h
│   ├── helperPolyphaseCorrelator.c # Corrélation temporelle directe
│   ├── tmwtypes.h                  # Types MATLAB (créé manuellement)
│   └── ... (25 autres fichiers)
├── archive/                        # Ancien code archivé
│   ├── dsss_demod.c                # Implémentation manuelle (1305 lignes)
│   └── dsss_demod.h                # Ancien header
└── Makefile                        # Modifié pour compiler avec MATLAB
```

---

## Fichiers Modifiés

### 1. `dsss_demod_matlab.c` (NOUVEAU)

Adapter qui convertit entre notre API et l'API MATLAB:

```c
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps, float fs, int max_doppler,
                       uint8_t *output_bits)
{
    // Convertit float complex → creal_T (double complex)
    // Configure settings MATLAB (velocity, txPower, oversampling)
    // Appelle dsss_receiver() MATLAB
    // Convertit boolean_T[202] → uint8_t[250]
    return rxStatus ? 0 : -1;
}
```

**Responsabilités:**
- Conversion de types (float → double, complex → creal_T)
- Validation des paramètres (sps=8 requis, 307200 samples minimum)
- Appel au code MATLAB Coder
- Conversion du résultat (202 bits MATLAB → 250 bits format attendu)

### 2. `test_matlab_coder/tmwtypes.h` (NOUVEAU)

Définitions de types MATLAB pour compilation standalone (sans installation MATLAB):

```c
typedef double real_T;
typedef struct { real_T re; real_T im; } creal_T;
typedef unsigned char boolean_T;
// + 150 lignes de types et macros
```

### 3. `Makefile` (MODIFIÉ)

Compilation avec les 29 fichiers MATLAB Coder:

```makefile
DSSS_SRC = dsss_demod_matlab.c  # Au lieu de dsss_demod.c

MATLAB_SRCS = \
    $(MATLAB_DIR)/dsss_receiver.c \
    $(MATLAB_DIR)/helperPolyphaseCorrelator.c \
    ... (12 fichiers)

dec406_dsss_test: test_dsss_main.o dsss_demod_matlab.o $(MATLAB_OBJS)
    $(CC) $(CFLAGS) -I$(MATLAB_DIR) -o $@ $^ $(LDFLAGS)
```

### 4. `dsss_demod.h` (REMPLACÉ)

Nouvelle version documentant l'API MATLAB:

- Documente les 7 étapes de traitement MATLAB
- Spécifie les contraintes (sps=8, 307200 samples)
- Marque les fonctions PRN comme dépréciées (MATLAB les gère en interne)

---

## Résultats Après Migration

### Test avec `test_frame_sps8.sigmf-data`

```
=== MATLAB Coder DSSS Receiver ===
Converting 307200 samples from float to double precision...
Settings: velocity=0.0, txPower=1.0, oversampling=4.0 (sps=8)
Calling MATLAB dsss_receiver...

=== MATLAB Results ===
rxStatus: SUCCESS
BCH Errors: 0
Converting 202 information bits to output...
SUCCESS: Decoded 202 information bits with 0 BCH errors

=== 406 MHz SECOND GENERATION BEACON (SGB) ===
[IDENTIFICATION]
 23 Hex ID: 80B4278DE563CB6DF706398
 TAC Number: 2531 (0x09E3)
 Serial Number: 7766 (0x1E56)
 Country Code: 5 (Unknown)
 Type: ELT
```

✅ **100% de réussite sur fichier test**
✅ **0 erreur BCH**
✅ **Message 2G complètement décodé**

---

## Code MATLAB Coder - Détails Techniques

### Algorithmes Implémentés Correctement

#### 1. AGC (Automatic Gain Control)
```c
// Update power every 10*sps samples (MATLAB exact)
if (i + 1 == 1) {
    avgPower = |input[0]|²;
} else if ((i+1) % (10*sps) == 1) {
    avgPower = 0.9 * avgPower + 0.1 * |input[i]|²;
}
gain = 1.0 / sqrt(avgPower);
```

#### 2. Carrier Synchronizer
```c
// Processes ALL samples (not decimated)
for (i = 0; i < length; i++) {
    // Apply phase rotation to EVERY sample
    output[i] = input[i] * exp(-j*phase);

    // Phase error with previous sample (i-1)
    if (i > 0) {
        error = atan2(output[i] * conj(output[i-1])) / 4.0;  // OQPSK
        freq += 0.001 * error;
        phase += freq + 0.01 * error;
    }
}
```

#### 3. Polyphase Correlator
```c
// Direct temporal correlation (NOT FFT)
for (i = 0; i <= nA; i++) {
    for (k = 0; k < ref_len; k++) {
        C[i+k] += A[i] * conj(B[k]);  // Time-domain convolution
    }
}

// Max per phase
for (phase = 0; phase < sps; phase++) {
    max_corr[phase] = max(|C[phase::sps]|);
}
```

---

## Interface API

### Fonction Principale

```c
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,              // Must be 8
                       float fs,             // 307200 Hz nominal
                       int max_doppler,      // Unused
                       uint8_t *output_bits); // 202 info bits + 48 zeros
```

### Contraintes MATLAB

- **sps:** Doit être exactement 8
- **Samples:** Minimum 307200 (38.4 kHz × 8 sps)
- **Doppler:** Pas de recherche Doppler (signal doit être pré-centré)
- **Sortie:** 202 bits d'information (48 bits de parité calculés mais non retournés)

---

## Compilation et Utilisation

### Compilation

```bash
make clean
make dec406_dsss_test
```

### Test

```bash
./dec406_dsss_test <fichier.iq> cf32 8
```

**Formats supportés:**
- `cf32`: Complex float32 (GQRX, SDR#, etc.)
- `cs16`: Complex int16
- `cu8`: Complex uint8 (RTL-SDR)

### Exemple

```bash
./dec406_dsss_test test_frame_sps8.sigmf-data cf32 8
```

---

## Fichiers Archivés

Les fichiers suivants ont été déplacés dans `archive/`:

- `dsss_demod.c` (1305 lignes) - Implémentation manuelle avec 7 divergences
- `dsss_demod.h` (ancienne version) - API originale

**Conservation:** Ces fichiers sont conservés comme référence historique et peuvent servir pour:
- Comparaison d'algorithmes
- Documentation des erreurs à éviter
- Backup en cas de problème avec MATLAB Coder

---

## Maintenance Future

### Régénération depuis MATLAB

Si modification du code MATLAB nécessaire:

1. Modifier `dsss_receiver.m` dans MATLAB
2. Régénérer avec MATLAB Coder:
   ```matlab
   codegen -config cfg dsss_receiver -args {coder.typeof(complex(0), [307200 1]), settings_struct}
   ```
3. Copier les fichiers générés dans `test_matlab_coder/`
4. Recompiler: `make clean && make`

### Tests de Non-Régression

Fichier test de référence: `/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/tools/test_frame_sps8.sigmf-data`

**Résultat attendu:**
- rxStatus: SUCCESS
- BCH Errors: 0
- 202 bits décodés
- TAC: 2531, Serial: 7766

---

## Conclusion

✅ Migration réussie vers MATLAB Coder
✅ 100% compatibilité MATLAB garantie
✅ Décodage fonctionnel validé
✅ Architecture propre et maintenable

**Leçon apprise:** Pour des algorithmes complexes de traitement du signal, utiliser directement le code généré par l'outil de référence (MATLAB Coder) est plus fiable que tenter une réécriture manuelle.
