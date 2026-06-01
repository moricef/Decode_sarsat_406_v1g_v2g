#!/usr/bin/env python3
"""
Quick hex extraction from demodulated output
Just shows the TX frame hex vs RX frame hex
"""
import sys

# TX frame (from t018_print_frame output)
tx_hex = "89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4"

print("=" * 70)
print("HEX COMPARISON: TX vs RX")
print("=" * 70)
print()
print("TX FRAME (252 bits = 63 hex nibbles):")
print(tx_hex)
print()

if len(sys.argv) < 2:
    print("Usage: ./quick_hex_extract.py <test_file.iq>")
    print("This will demodulate and show RX hex")
    sys.exit(1)

import subprocess
import numpy as np

filename = sys.argv[1]

# Quick demod using Python (bypass OpenMP slowness)
print("Loading IQ file...")
iq_data = np.fromfile(filename, dtype=np.float32)
I = iq_data[0::2]
Q = iq_data[1::2]

print(f"Samples: {len(I)}")

# Extract chip rate samples
sample_rate = 2.5e6
chip_rate = 38400
sps = sample_rate / chip_rate

print(f"Extracting chips at {sps:.2f} samples/chip...")

# Find best chip offset
best_offset = 0
best_power = 0

for offset in range(int(sps)):
    indices = np.arange(offset, min(len(I), int(sps * 1000)), int(sps))
    if len(indices) < 100:
        continue
    power = np.mean(I[indices]**2)
    if power > best_power:
        best_power = power
        best_offset = offset

print(f"Best offset: {best_offset}")

# Extract all 38,400 chips
indices = np.arange(best_offset, len(I), int(sps))[:38400]
I_chips = I[indices]
Q_chips = Q[indices]

print(f"Extracted {len(I_chips)} chips")

# Convert to binary (simple hard decision)
I_bits = (I_chips > 0).astype(int)
Q_bits = (Q_chips > 0).astype(int)

# Generate PRN (simplified - using same LFSR as TX)
def lfsr_step(lfsr):
    feedback = ((lfsr >> 0) ^ (lfsr >> 18)) & 1
    return (lfsr >> 1) | (feedback << 22)

def generate_prn(channel, num_bits=150):
    lfsr = 0x001
    chips = []
    for bit_idx in range(num_bits):
        for chip_idx in range(256):
            if channel == 'I':
                chip = 1 if (lfsr & 1) else 0
            else:
                chip = 1 if ((lfsr >> 18) & 1) else 0
            chips.append(chip)
            lfsr = lfsr_step(lfsr)
    return np.array(chips)

print("Generating PRN sequences...")
prn_I = generate_prn('I', 150)
prn_Q = generate_prn('Q', 150)

# Despread (XOR then count)
print("Despreading...")
i_despread = []
q_despread = []

for bit in range(150):
    start = bit * 256
    end = start + 256

    i_chip_block = I_bits[start:end]
    q_chip_block = Q_bits[start:end]

    i_prn_block = prn_I[start:end]
    q_prn_block = prn_Q[start:end]

    # XOR and sum
    i_xor = np.bitwise_xor(i_chip_block, i_prn_block)
    q_xor = np.bitwise_xor(q_chip_block, q_prn_block)

    i_sum = np.sum(i_xor)
    q_sum = np.sum(q_xor)

    # ML decode: if sum > 128, bit=1, else bit=0
    i_despread.append(1 if i_sum > 128 else 0)
    q_despread.append(1 if q_sum > 128 else 0)

# Interleave I/Q bits
output_bits = []
for i in range(150):
    output_bits.append(i_despread[i])
    output_bits.append(q_despread[i])

# Convert to hex
print()
print("RX FRAME (300 bits → first 252 bits):")
rx_hex = ""
for i in range(63):  # 252 bits = 63 nibbles
    nibble = 0
    for j in range(4):
        if output_bits[i*4 + j]:
            nibble |= (1 << (3 - j))
    rx_hex += f"{nibble:X}"

print(rx_hex)
print()

# Compare
print("=" * 70)
print("COMPARISON:")
print("=" * 70)
matches = sum(1 for a, b in zip(tx_hex, rx_hex) if a == b)
print(f"Matching nibbles: {matches}/63 ({matches/63*100:.1f}%)")

# Show differences
print()
print("Differences (TX → RX):")
for i in range(63):
    if tx_hex[i] != rx_hex[i]:
        print(f"  Position {i:2d}: {tx_hex[i]} → {rx_hex[i]}")

print("=" * 70)
