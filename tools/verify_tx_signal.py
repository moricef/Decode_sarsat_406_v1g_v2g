#!/usr/bin/env python3
"""
Verify TX signal correctness by direct PRN correlation
Bypasses the entire demodulator chain to isolate TX issues
"""

import numpy as np
import sys

def lfsr_step(lfsr):
    """T.018 LFSR: X^23 + X^18 + 1"""
    feedback = ((lfsr >> 0) ^ (lfsr >> 18)) & 1
    lfsr = (lfsr >> 1) | (feedback << 22)
    return lfsr

def generate_prn_sequence(channel, num_bits=150):
    """Generate complete PRN sequence for I or Q channel"""
    lfsr = 0x001  # T.018 initial state
    chips = []

    for bit_idx in range(num_bits):
        for chip_idx in range(256):
            if channel == 'I':
                chip = -1 if (lfsr & 1) else +1  # X0
            else:  # Q
                chip = -1 if ((lfsr >> 18) & 1) else +1  # X18
            chips.append(chip)
            lfsr = lfsr_step(lfsr)

    return np.array(chips)

def test_signal_quality(filename):
    """
    Test 1: Verify signal has correct structure
    - Sample rate, duration, power levels
    """
    print("=" * 60)
    print("TEST 1: SIGNAL STRUCTURE")
    print("=" * 60)

    iq_data = np.fromfile(filename, dtype=np.float32)
    I = iq_data[0::2]
    Q = iq_data[1::2]

    print(f"File: {filename}")
    print(f"Total samples: {len(I)}")
    print(f"Expected: 2,500,000 samples (1 second @ 2.5 MHz)")

    if len(I) != 2500000:
        print(f"❌ FAIL: Wrong sample count")
        return False
    else:
        print(f"✅ PASS: Sample count correct")

    # Check power levels
    power = np.mean(I**2 + Q**2)
    print(f"\nAverage power: {power:.3f}")
    print(f"Expected: ~1.0-2.0 (chips at ±1)")

    if 0.5 < power < 3.0:
        print(f"✅ PASS: Power level reasonable")
    else:
        print(f"❌ FAIL: Power level abnormal")
        return False

    return True

def test_chip_sampling(filename):
    """
    Test 2: Verify chips can be extracted at chip rate
    - 38,400 chips should be recoverable
    """
    print("\n" + "=" * 60)
    print("TEST 2: CHIP RATE SAMPLING")
    print("=" * 60)

    iq_data = np.fromfile(filename, dtype=np.float32)
    I = iq_data[0::2]
    Q = iq_data[1::2]

    sample_rate = 2.5e6
    chip_rate = 38400
    sps = sample_rate / chip_rate  # 65.104 samples/chip

    print(f"Sample rate: {sample_rate/1e6:.1f} MHz")
    print(f"Chip rate: {chip_rate} chips/s")
    print(f"Samples per chip: {sps:.3f}")

    # Extract chips at multiple offsets to find best alignment
    best_offset = 0
    best_stability = 0

    for offset in range(0, int(sps)):
        chip_indices = np.arange(offset, len(I), int(sps))[:38400]
        if len(chip_indices) < 38400:
            continue

        I_chips = I[chip_indices]

        # Measure stability: chips should be close to ±1
        stability = np.mean((np.abs(I_chips) > 0.5))

        if stability > best_stability:
            best_stability = stability
            best_offset = offset

    print(f"\nBest sampling offset: {best_offset} samples")
    print(f"Chip stability (|chip| > 0.5): {best_stability*100:.1f}%")

    if best_stability > 0.8:
        print(f"✅ PASS: Chips are stable")
        return True, best_offset
    else:
        print(f"❌ FAIL: Chips are unstable (interpolation issues?)")
        return False, 0

def test_preamble_correlation(filename, chip_offset=0):
    """
    Test 3: Verify preamble (50 bits of '0') correlates with PRN
    - First 12,800 chips should match PRN for bit=0
    """
    print("\n" + "=" * 60)
    print("TEST 3: PREAMBLE PRN CORRELATION")
    print("=" * 60)

    iq_data = np.fromfile(filename, dtype=np.float32)
    I = iq_data[0::2]
    Q = iq_data[1::2]

    sample_rate = 2.5e6
    chip_rate = 38400
    sps = sample_rate / chip_rate

    # Extract chips
    chip_indices = np.arange(chip_offset, len(I), int(sps))[:38400]
    I_chips = I[chip_indices]
    Q_chips = Q[chip_indices]

    # Generate expected PRN for preamble (25 I-bits + 25 Q-bits, all '0')
    # Preamble: 50 bits → odd bits (25) go to I, even bits (25) go to Q
    prn_I = generate_prn_sequence('I', num_bits=25)  # 6400 chips
    prn_Q = generate_prn_sequence('Q', num_bits=25)  # 6400 chips

    # T.018: bit=0 means what? Need to test both polarities
    # Convention 1: bit=0 → PRN as-is
    # Convention 2: bit=0 → PRN inverted

    results = []

    for i_polarity in [-1, +1]:
        for q_polarity in [-1, +1]:
            # Correlate
            corr_I = np.sum(np.sign(I_chips[:6400]) == np.sign(prn_I * i_polarity)) / 6400
            corr_Q = np.sum(np.sign(Q_chips[:6400]) == np.sign(prn_Q * q_polarity)) / 6400
            avg_corr = (corr_I + corr_Q) / 2

            results.append({
                'i_pol': i_polarity,
                'q_pol': q_polarity,
                'corr_I': corr_I,
                'corr_Q': corr_Q,
                'avg': avg_corr
            })

    # Find best result
    best = max(results, key=lambda x: x['avg'])

    print(f"\nBest correlation:")
    print(f"  I-channel: {best['corr_I']*100:.1f}% (polarity={best['i_pol']:+d})")
    print(f"  Q-channel: {best['corr_Q']*100:.1f}% (polarity={best['q_pol']:+d})")
    print(f"  Average: {best['avg']*100:.1f}%")

    if best['avg'] > 0.95:
        print(f"✅ PASS: Excellent PRN correlation - TX signal is CORRECT")
        return True
    elif best['avg'] > 0.85:
        print(f"⚠️  WARNING: Good but not perfect - possible minor TX issue")
        return True
    elif best['avg'] > 0.70:
        print(f"⚠️  WARNING: Moderate correlation - TX has issues")
        return False
    else:
        print(f"❌ FAIL: Poor correlation - TX signal is INCORRECT")
        return False

def test_oqpsk_offset(filename, chip_offset=0):
    """
    Test 4: Verify OQPSK Tc/2 delay between I and Q
    - I and Q should be offset by half a chip period
    """
    print("\n" + "=" * 60)
    print("TEST 4: OQPSK Tc/2 DELAY VERIFICATION")
    print("=" * 60)

    iq_data = np.fromfile(filename, dtype=np.float32)
    I = iq_data[0::2]
    Q = iq_data[1::2]

    sample_rate = 2.5e6
    chip_rate = 38400
    sps = sample_rate / chip_rate  # ~65 samples/chip
    half_chip = int(sps / 2)  # ~32 samples

    # Extract I chips at t=0
    chip_indices = np.arange(chip_offset, len(I), int(sps))[:1000]
    I_chips = I[chip_indices]

    # Extract Q chips at t=0 and t=Tc/2
    Q_chips_aligned = Q[chip_indices]  # Same timing as I
    Q_chips_delayed = Q[chip_indices + half_chip]  # Delayed by Tc/2

    # Measure correlation: I should match Q delayed, NOT Q aligned
    # For OQPSK: I[n] corresponds to Q[n-0.5] not Q[n]

    # Simple test: count transitions
    # In OQPSK, I and Q transitions should NOT happen simultaneously
    I_transitions = np.sum(np.abs(np.diff(np.sign(I_chips))) > 0)
    Q_transitions = np.sum(np.abs(np.diff(np.sign(Q_chips_aligned))) > 0)

    # Check if transitions are offset
    I_trans_indices = np.where(np.abs(np.diff(np.sign(I_chips))) > 0)[0]
    Q_trans_indices = np.where(np.abs(np.diff(np.sign(Q_chips_aligned))) > 0)[0]

    # Count simultaneous transitions (should be rare in OQPSK)
    simultaneous = 0
    for i_idx in I_trans_indices:
        if np.any(np.abs(Q_trans_indices - i_idx) < 3):  # Within 3 samples
            simultaneous += 1

    sim_rate = simultaneous / max(len(I_trans_indices), 1)

    print(f"\nI transitions: {I_transitions}")
    print(f"Q transitions: {Q_transitions}")
    print(f"Simultaneous transitions: {simultaneous} ({sim_rate*100:.1f}%)")
    print(f"Expected for OQPSK: <10% simultaneous")

    if sim_rate < 0.2:
        print(f"✅ PASS: OQPSK offset detected")
        return True
    else:
        print(f"⚠️  WARNING: High simultaneous transitions (may be QPSK, not OQPSK)")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: ./verify_tx_signal.py <file.iq>")
        sys.exit(1)

    filename = sys.argv[1]

    print("╔════════════════════════════════════════════════════════════╗")
    print("║       TX SIGNAL VERIFICATION SUITE                        ║")
    print("║       COSPAS-SARSAT T.018 Second Generation               ║")
    print("╚════════════════════════════════════════════════════════════╝")
    print()

    # Run all tests
    test1_pass = test_signal_quality(filename)
    test2_pass, chip_offset = test_chip_sampling(filename)
    test3_pass = test_preamble_correlation(filename, chip_offset)
    test4_pass = test_oqpsk_offset(filename, chip_offset)

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    tests = [
        ("Signal Structure", test1_pass),
        ("Chip Sampling", test2_pass),
        ("PRN Correlation", test3_pass),
        ("OQPSK Offset", test4_pass)
    ]

    for name, passed in tests:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"{name:30s} {status}")

    all_pass = all(t[1] for t in tests)

    print("\n" + "=" * 60)
    if all_pass:
        print("✅ TX SIGNAL IS CORRECT")
        print("Problem is in RX demodulator, not in TX")
    else:
        print("❌ TX SIGNAL HAS ISSUES")
        print("Fix TX before debugging RX")
    print("=" * 60)

if __name__ == "__main__":
    main()
