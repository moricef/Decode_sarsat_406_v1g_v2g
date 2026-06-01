#!/usr/bin/env python3
"""
Analyze IQ constellation of COSPAS-SARSAT signal
Verify chip distribution for OQPSK with rectangular chips (no RRC)
"""

import numpy as np
import sys

def analyze_iq_file(filename, sample_rate=2.5e6):
    """Load and analyze IQ file constellation"""

    # Load IQ data (32-bit float interleaved)
    iq_data = np.fromfile(filename, dtype=np.float32)
    I = iq_data[0::2]
    Q = iq_data[1::2]

    print(f"=== IQ FILE ANALYSIS ===")
    print(f"File: {filename}")
    print(f"Total samples: {len(I)}")
    print(f"Duration: {len(I)/sample_rate*1000:.1f} ms")
    print()

    # Sample at chip rate (38.4 kchips/s)
    chip_rate = 38400
    sps = sample_rate / chip_rate  # 65.104 samples/chip

    # Extract chip samples (at chip boundaries)
    chip_indices = np.arange(0, len(I), int(sps))[:38400]
    I_chips = I[chip_indices]
    Q_chips = Q[chip_indices]

    print(f"=== CHIP-RATE SAMPLING ===")
    print(f"Chip rate: {chip_rate} chips/s")
    print(f"Samples per chip: {sps:.3f}")
    print(f"Total chips sampled: {len(I_chips)}")
    print()

    # Analyze I/Q ranges
    print(f"=== I/Q RANGES ===")
    print(f"I: min={I_chips.min():.6f}, max={I_chips.max():.6f}")
    print(f"Q: min={Q_chips.min():.6f}, max={Q_chips.max():.6f}")
    print()

    # Expected for rectangular chips (no RRC): ±1.0 exactly
    # Check how many chips are exactly ±1.0
    tolerance = 0.01
    I_is_plus1 = np.abs(I_chips - 1.0) < tolerance
    I_is_minus1 = np.abs(I_chips + 1.0) < tolerance
    Q_is_plus1 = np.abs(Q_chips - 1.0) < tolerance
    Q_is_minus1 = np.abs(Q_chips + 1.0) < tolerance

    I_exact = np.sum(I_is_plus1 | I_is_minus1) / len(I_chips) * 100
    Q_exact = np.sum(Q_is_plus1 | Q_is_minus1) / len(Q_chips) * 100

    print(f"=== CHIP LEVELS (tolerance={tolerance}) ===")
    print(f"I chips at ±1.0: {I_exact:.1f}%")
    print(f"Q chips at ±1.0: {Q_exact:.1f}%")
    print()

    # Constellation quadrants (for QPSK, chips should be on axes)
    # Expected for OQPSK: chips on (±1, ~0) or (~0, ±1)
    threshold = 0.5

    quad_pp = np.sum((I_chips > threshold) & (Q_chips > threshold))
    quad_pn = np.sum((I_chips > threshold) & (Q_chips < -threshold))
    quad_np = np.sum((I_chips < -threshold) & (Q_chips > threshold))
    quad_nn = np.sum((I_chips < -threshold) & (Q_chips < -threshold))
    on_I_axis = np.sum((np.abs(I_chips) > threshold) & (np.abs(Q_chips) < threshold))
    on_Q_axis = np.sum((np.abs(Q_chips) > threshold) & (np.abs(I_chips) < threshold))

    total = len(I_chips)

    print(f"=== CONSTELLATION DISTRIBUTION ===")
    print(f"Quadrant I   (+I,+Q): {quad_pp:5d} ({quad_pp/total*100:5.1f}%)")
    print(f"Quadrant II  (-I,+Q): {quad_np:5d} ({quad_np/total*100:5.1f}%)")
    print(f"Quadrant III (-I,-Q): {quad_nn:5d} ({quad_nn/total*100:5.1f}%)")
    print(f"Quadrant IV  (+I,-Q): {quad_pn:5d} ({quad_pn/total*100:5.1f}%)")
    print(f"On I-axis (Q~0):      {on_I_axis:5d} ({on_I_axis/total*100:5.1f}%)")
    print(f"On Q-axis (I~0):      {on_Q_axis:5d} ({on_Q_axis/total*100:5.1f}%)")
    print()

    # Expected: ~100% on I or Q axis for rectangular OQPSK
    on_axes = on_I_axis + on_Q_axis
    print(f"EXPECTED for rectangular OQPSK: ~100% on axes")
    print(f"MEASURED: {on_axes/total*100:.1f}% on axes")

    if on_axes/total > 0.9:
        print("✅ PASS: Chips are on QPSK axes (rectangular chips)")
    else:
        print("❌ FAIL: Chips distributed in quadrants (NOT rectangular)")
    print()

    # Check first 256 I-chips for PRN correlation
    print(f"=== PRN CORRELATION TEST ===")
    print(f"Testing first 256 I-chips against expected PRN...")

    # Expected PRN for first bit (I-channel, T.018 Table 2.2)
    # This is just a sanity check - full PRN validation requires complete sequence
    chips_binary = (I_chips[:256] > 0).astype(int)

    # Count transitions (PRN should have ~50% transitions)
    transitions = np.sum(np.diff(chips_binary) != 0)
    transition_rate = transitions / 255 * 100

    print(f"Chip transitions in first 256 chips: {transitions}/255 ({transition_rate:.1f}%)")
    print(f"Expected for PRN: ~40-60% (pseudorandom)")

    if 40 <= transition_rate <= 60:
        print("✅ PASS: Transition rate consistent with PRN")
    else:
        print("⚠️  WARNING: Unusual transition rate")

    return I_chips, Q_chips

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: ./analyze_constellation.py <file.iq>")
        sys.exit(1)

    analyze_iq_file(sys.argv[1])
