#!/bin/bash
# pipeline_resample.sh

INPUT="Fichiers_IQ/test_known.iq"
TEMP_NORM="Fichiers_IQ/temp_normalized.iq"
OUTPUT="Fichiers_IQ/test_known_rech.iq"

echo "=== Pipeline de rééchantillonnage OQPSK ==="

# Étape 1: Vérifier l'amplitude originale
echo "1. Analyse de l'amplitude originale..."
python3 -c "
import numpy as np
samples = np.fromfile('$INPUT', dtype=np.float32)
iq = samples[::2] + 1j * samples[1::2]
max_val = np.max(np.abs(iq))
print(f'   Amplitude max: {max_val:.3f}')
print(f'   Nombre d\\'échantillons: {len(iq)}')
print(f'   Durée: {len(iq)/2500000:.3f}s')
"

# Étape 2: Normalisation
echo "2. Normalisation..."
sox -t f32 -r 2500000 -c 2 "$INPUT" -t f32 "$TEMP_NORM" gain -n -3.0

# Étape 3: Rééchantillonnage
echo "3. Rééchantillonnage 2.5MHz → 307.2kHz..."
sox -t f32 -r 2500000 -c 2 "$TEMP_NORM" -t f32 -r 307200 "$OUTPUT" rate -s 307200

# Étape 4: Vérification finale
echo "4. Vérification du résultat..."
python3 -c "
import numpy as np
samples = np.fromfile('$OUTPUT', dtype=np.float32)
iq = samples[::2] + 1j * samples[1::2]
max_val = np.max(np.abs(iq))
print(f'   Amplitude max après rééchantillonnage: {max_val:.3f}')
print(f'   Nouveau nombre d\\'échantillons: {len(iq)}')
print(f'   Nouvelle durée: {len(iq)/307200:.3f}s')
"

# Nettoyage
rm "$TEMP_NORM"

echo "=== Rééchantillonnage terminé ==="
echo "Fichier output: $OUTPUT"
