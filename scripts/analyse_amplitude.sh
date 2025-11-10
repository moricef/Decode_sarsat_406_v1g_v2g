#!/bin/bash
# analyse_amplitude.sh

INPUT="Fichiers_IQ/test_known_resampled.iq"

echo "=== Analyse détaillée du fichier IQ ==="

python3 -c "
import numpy as np
import sys

# Lire le fichier IQ
samples = np.fromfile('$INPUT', dtype=np.float32)
if len(samples) % 2 != 0:
    print('ERREUR: Nombre impair d\\'échantillons!')
    sys.exit(1)
    
iq = samples[::2] + 1j * samples[1::2]

print(f'Nombre total d\\'échantillons: {len(iq)}')
print(f'Durée: {len(iq)/2500000:.3f} secondes')

# Analyse d'amplitude
amplitudes = np.abs(iq)
max_amp = np.max(amplitudes)
min_amp = np.min(amplitudes)
mean_amp = np.mean(amplitudes)
std_amp = np.std(amplitudes)

print(f'Amplitude MAX: {max_amp:.3f}')
print(f'Amplitude MIN: {min_amp:.3f}') 
print(f'Amplitude MOYENNE: {mean_amp:.3f}')
print(f'Écart-type: {std_amp:.3f}')

# Compter les échantillons qui clippent
clipping_count = np.sum(amplitudes > 1.0)
clipping_percent = (clipping_count / len(iq)) * 100

print(f'Échantillons qui clippent (>1.0): {clipping_count}/{len(iq)} ({clipping_percent:.2f}%)')

# Analyse des valeurs extrêmes
print(f'Valeur I max: {np.max(iq.real):.3f}')
print(f'Valeur I min: {np.min(iq.real):.3f}')
print(f'Valeur Q max: {np.max(iq.imag):.3f}')
print(f'Valeur Q min: {np.min(iq.imag):.3f}')

# Vérifier les NaN/Inf
nan_count = np.sum(np.isnan(iq))
inf_count = np.sum(np.isinf(iq))
print(f'Valeurs NaN: {nan_count}')
print(f'Valeurs Inf: {inf_count}')

# Histogramme des amplitudes
hist, bins = np.histogram(amplitudes, bins=50, range=(0, 2))
print('\\nHistogramme des amplitudes:')
for i in range(len(hist)):
    if hist[i] > 0:
        print(f'  {bins[i]:.2f}-{bins[i+1]:.2f}: {hist[i]} échantillons')
"

echo "=== Analyse terminée ==="
