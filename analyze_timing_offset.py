#!/usr/bin/env python3
"""
Analyse les positions d'échantillonnage du timing recovery
vs les positions théoriques idéales
"""

# Positions idéales (extraction simple)
SAMPLES_PER_CHIP = 65.104166667

print("=" * 70)
print("ANALYSE TIMING RECOVERY vs EXTRACTION SIMPLE")
print("=" * 70)
print()

# Lire les positions du timing recovery
timing_positions = []
with open('timing_positions.txt', 'r') as f:
    for line in f:
        if line.startswith('#'):
            continue
        parts = line.strip().split()
        if len(parts) >= 4:
            idx = int(parts[0])
            timing_phase = float(parts[1])
            center_idx = int(parts[2])
            mu = float(parts[3])
            timing_positions.append((idx, timing_phase, center_idx, mu))

print(f"Loaded {len(timing_positions)} timing positions\n")

print("Symbol | Ideal Position | Timing Recovery | Offset (samples)")
print("-------|----------------|-----------------|------------------")

for idx, timing_phase, center_idx, mu in timing_positions[:20]:
    ideal_pos = idx * SAMPLES_PER_CHIP
    offset = timing_phase - ideal_pos
    print(f"{idx:6d} | {ideal_pos:14.2f} | {timing_phase:15.2f} | {offset:+8.2f}")

print()
print("OBSERVATIONS:")
print()

# Offset au début
first_offset = timing_positions[0][1] - (timing_positions[0][0] * SAMPLES_PER_CHIP)
print(f"1. Offset initial: {first_offset:+.2f} samples (timing starts at {timing_positions[0][1]:.0f} instead of 0)")

# Dérive
if len(timing_positions) >= 100:
    last_idx = timing_positions[99][0]
    last_timing = timing_positions[99][1]
    ideal_last = last_idx * SAMPLES_PER_CHIP
    last_offset = last_timing - ideal_last
    drift = last_offset - first_offset
    print(f"2. Offset at symbol 99: {last_offset:+.2f} samples")
    print(f"3. Total drift: {drift:+.2f} samples over 99 symbols ({drift/99:.4f} samples/symbol)")

# Moyenne des offsets
total_offset = sum(pos[1] - (pos[0] * SAMPLES_PER_CHIP) for pos in timing_positions[:20])
avg_offset = total_offset / 20
print(f"4. Average offset (first 20): {avg_offset:+.2f} samples")

print()
print("CONCLUSION:")
if abs(avg_offset) > 1:
    print(f"⚠️  Le timing recovery a un offset moyen de {avg_offset:+.2f} samples")
    print("   Cela explique pourquoi la corrélation est faible (62-64% au lieu de 100%)")
    print()
    print("CAUSE PROBABLE:")
    print("   - Le timing recovery initialise timing_phase à 2.0 (ligne 880 dsss_demod.c)")
    print("   - Il devrait commencer à 0.0 pour un signal synchrone parfait")
else:
    print("✓  Le timing recovery est bien aligné")
