#!/usr/bin/env python3
"""
Test all phase rotations and I/Q swaps to find correct combination
"""

# Expected frame from generator
PREAMBLE_BITS = 50
PAYLOAD_HEX = "89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4"

# Demodulated frame
DEMODULATED_HEX = "B8D0EE0EF7AB69EB41B70F0DD30C7E1554151344C05CAB652A43418FAD68757A71E7DC635E0A"

def hex_to_bits(hex_string):
    bits = []
    for char in hex_string:
        value = int(char, 16)
        for i in range(3, -1, -1):
            bits.append((value >> i) & 1)
    return bits

def build_expected_frame():
    preamble = [i % 2 for i in range(PREAMBLE_BITS)]
    payload = hex_to_bits(PAYLOAD_HEX)
    return preamble + payload

def apply_transformation(bits, rotation, iq_swap, invert):
    """
    Apply transformations:
    - rotation: 0, 1, 2, 3 (0°, 90°, 180°, 270°)
    - iq_swap: False, True
    - invert: False, True (invert all bits)
    """
    result = bits[:]

    # Apply global bit inversion
    if invert:
        result = [1 - b for b in result]

    # I/Q swap not applicable here (would need symbol-level data)
    # Rotation also not directly applicable to bits

    return result

def test_all_shifts(expected, demodulated):
    """Test bit shifts from -20 to +20"""
    print("\n" + "="*80)
    print("TESTING BIT SHIFTS (-20 to +20)")
    print("="*80)

    best_shift = 0
    best_match = 0
    best_errors = len(expected)

    for shift in range(-20, 21):
        matches = 0
        errors = 0

        for i in range(len(expected)):
            shifted_idx = i + shift
            if 0 <= shifted_idx < len(demodulated):
                if expected[i] == demodulated[shifted_idx]:
                    matches += 1
                else:
                    errors += 1

        if matches > best_match:
            best_match = matches
            best_shift = shift
            best_errors = errors

        if abs(shift) <= 10 or matches > len(expected) * 0.7:
            print(f"  Shift {shift:+3d}: {matches:3d}/{len(expected)} matches " +
                  f"({matches*100.0/len(expected):5.1f}%) - {errors} errors")

    print(f"\n✅ BEST SHIFT: {best_shift} bits")
    print(f"   Matches: {best_match}/{len(expected)} ({best_match*100.0/len(expected):.1f}%)")
    print(f"   Errors: {best_errors}")

    return best_shift, best_match

def test_inversions(expected, demodulated):
    """Test bit inversion"""
    print("\n" + "="*80)
    print("TESTING BIT INVERSION")
    print("="*80)

    # Test as-is
    matches_normal = sum(1 for i in range(min(len(expected), len(demodulated)))
                        if expected[i] == demodulated[i])

    # Test inverted
    demod_inverted = [1 - b for b in demodulated]
    matches_inverted = sum(1 for i in range(min(len(expected), len(demod_inverted)))
                          if expected[i] == demod_inverted[i])

    print(f"  Normal:   {matches_normal}/{len(expected)} matches " +
          f"({matches_normal*100.0/len(expected):.1f}%)")
    print(f"  Inverted: {matches_inverted}/{len(expected)} matches " +
          f"({matches_inverted*100.0/len(expected):.1f}%)")

    if matches_inverted > matches_normal:
        print(f"\n✅ INVERSION IMPROVES: +{matches_inverted - matches_normal} matches")
        print("   Fix: Invert all chips in demodulator")
        return True, demod_inverted
    else:
        print(f"\n❌ Inversion does not help")
        return False, demodulated

def test_combined(expected, demodulated):
    """Test combination of shift + inversion"""
    print("\n" + "="*80)
    print("TESTING SHIFT + INVERSION COMBINED")
    print("="*80)

    best_shift = 0
    best_invert = False
    best_match = 0

    for invert in [False, True]:
        test_bits = [1 - b for b in demodulated] if invert else demodulated

        for shift in range(-20, 21):
            matches = 0

            for i in range(len(expected)):
                shifted_idx = i + shift
                if 0 <= shifted_idx < len(test_bits):
                    if expected[i] == test_bits[shifted_idx]:
                        matches += 1

            if matches > best_match:
                best_match = matches
                best_shift = shift
                best_invert = invert

    print(f"✅ BEST COMBINATION:")
    print(f"   Shift: {best_shift} bits")
    print(f"   Invert: {best_invert}")
    print(f"   Matches: {best_match}/{len(expected)} ({best_match*100.0/len(expected):.1f}%)")

    return best_shift, best_invert, best_match

def main():
    print("\n" + "="*80)
    print("PHASE/TRANSFORM TESTING")
    print("="*80)

    expected = build_expected_frame()
    demodulated = hex_to_bits(DEMODULATED_HEX)

    print(f"Expected: {len(expected)} bits")
    print(f"Demodulated: {len(demodulated)} bits")

    # Test shifts
    best_shift, _ = test_all_shifts(expected, demodulated)

    # Test inversions
    should_invert, demod_processed = test_inversions(expected, demodulated)

    # Test combined
    final_shift, final_invert, final_match = test_combined(expected, demodulated)

    # Summary
    print("\n" + "="*80)
    print("FINAL RECOMMENDATION")
    print("="*80)

    if final_match > len(expected) * 0.9:
        print("✅ SOLUTION FOUND!")
    elif final_match > len(expected) * 0.7:
        print("⚠️  PARTIAL SOLUTION (70-90% match)")
    else:
        print("❌ NO GOOD SOLUTION FOUND (<70% match)")

    print(f"\nApply these transformations:")
    print(f"  1. {'INVERT' if final_invert else 'DO NOT INVERT'} all bits")
    print(f"  2. Shift by {final_shift} bits ({'left' if final_shift < 0 else 'right'})")
    print(f"\nExpected result: {final_match}/{len(expected)} correct bits ({final_match*100.0/len(expected):.1f}%)")

    print("\n")

if __name__ == '__main__':
    main()
