#!/bin/bash
# resample_oqpsk_2500k_to_307k.sh
# Rééchantillonnage OQPSK 2.5 MHz → 307.2 kHz avec normalisation

INPUT="Fichiers_IQ/test_known.iq"
OUTPUT="Fichiers_IQ/test_known_resampled.iq"

echo "=== Rééchantillonnage OQPSK 2.5 MHz → 307.2 kHz ==="

# Normalisation basée sur l'analyse d'amplitude
python3 -c "
import numpy as np

samples = np.fromfile('$INPUT', dtype=np.float32)
iq = samples[::2] + 1j * samples[1::2]

# Facteur de normalisation pour éviter le clipping
scaling_factor = 0.7 / 1.420  # 70% de marge / amplitude max mesurée
iq_normalized = iq * scaling_factor

print(f'Amplitude originale max: 1.420')
print(f'Facteur de normalisation: {scaling_factor:.3f}')
print(f'Nouvelle amplitude max: {np.max(np.abs(iq_normalized)):.3f}')

# Sauvegarde temporaire
interleaved = np.zeros(2 * len(iq_normalized), dtype=np.float32)
interleaved[::2] = iq_normalized.real
interleaved[1::2] = iq_normalized.imag
interleaved.tofile('temp_normalized.iq')
"

# Rééchantillonnage
echo "Rééchantillonnage 2.5 MHz → 307.2 kHz..."
sox -t f32 -r 2500000 -c 2 temp_normalized.iq -t f32 -r 307200 "$OUTPUT" rate -s 307200

# Vérification
python3 -c "
import numpy as np
samples = np.fromfile('$OUTPUT', dtype=np.float32)
iq = samples[::2] + 1j * samples[1::2]

max_amp = np.max(np.abs(iq))
clipping_count = np.sum(np.abs(iq) > 1.0)

print(f'Vérification finale:')
print(f'  Amplitude max: {max_amp:.3f}')
print(f'  Échantillons qui clippent: {clipping_count}/{len(iq)}')
print(f'  Statut: {'✅ OK' if max_amp <= 1.0 else '❌ CLIPPING'}')
"

# Nettoyage
rm -f temp_normalized.iq

echo "=== Rééchantillonnage terminé ==="
echo "Fichier output: $OUTPUT"
