#!/usr/bin/env python3
"""
Compare expected frame with demodulated bits
"""

import sys

# Expected frame from generator
PREAMBLE_BITS = 50  # Alternating 0101010101...
PAYLOAD_HEX = "89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4"

# Demodulated frame from test (from debug output)
DEMODULATED_HEX = "B8D0EE0EF7AB69EB41B70F0DD30C7E1554151344C05CAB652A43418FAD68757A71E7DC635E0A"

def hex_to_bits(hex_string):
    """Convert hex string to list of bits"""
    bits = []
    for char in hex_string:
        value = int(char, 16)
        for i in range(3, -1, -1):  # 4 bits per hex char
            bits.append((value >> i) & 1)
    return bits

def build_expected_frame():
    """Build complete expected frame: 50 preamble + 252 payload"""
    # Preamble: alternating 0101010101...
    preamble = [i % 2 for i in range(PREAMBLE_BITS)]

    # Payload: convert hex to bits
    payload = hex_to_bits(PAYLOAD_HEX)

    return preamble + payload

def build_demodulated_frame():
    """Build demodulated frame from hex debug output"""
    return hex_to_bits(DEMODULATED_HEX)

def compare_frames(expected, demodulated):
    """Compare two bit arrays and identify error pattern"""

    print("="*80)
    print("BIT-BY-BIT COMPARISON")
    print("="*80)

    if len(expected) != len(demodulated):
        print(f"⚠️  LENGTH MISMATCH: expected {len(expected)}, got {len(demodulated)}")
        min_len = min(len(expected), len(demodulated))
    else:
        min_len = len(expected)

    # Count errors
    errors = 0
    error_positions = []

    for i in range(min_len):
        if expected[i] != demodulated[i]:
            errors += 1
            error_positions.append(i)

    print(f"\nTotal bits compared: {min_len}")
    print(f"Errors: {errors} ({errors*100.0/min_len:.1f}%)")
    print(f"Correct: {min_len - errors} ({(min_len-errors)*100.0/min_len:.1f}%)")

    # Show first 50 bits (preamble)
    print("\n" + "="*80)
    print("PREAMBLE (first 50 bits)")
    print("="*80)
    print("Expected: ", end="")
    for i in range(min(50, min_len)):
        print(expected[i], end="")
        if (i+1) % 10 == 0:
            print(" ", end="")
    print()

    print("Received: ", end="")
    for i in range(min(50, min_len)):
        if expected[i] == demodulated[i]:
            print(demodulated[i], end="")
        else:
            print(f"\033[91m{demodulated[i]}\033[0m", end="")  # Red for errors
        if (i+1) % 10 == 0:
            print(" ", end="")
    print()

    preamble_errors = sum(1 for i in range(min(50, min_len)) if expected[i] != demodulated[i])
    print(f"Preamble errors: {preamble_errors}/50 ({preamble_errors*100.0/50:.1f}%)")

    # Check for inversion pattern
    inverted_matches = sum(1 for i in range(min_len) if expected[i] != demodulated[i])
    if inverted_matches > min_len * 0.9:
        print("\n🔴 PATTERN: Almost all bits INVERTED!")
        print("   Fix: Invert all chips or decision logic")

    # Check for shift
    print("\n" + "="*80)
    print("CHECKING FOR BIT SHIFT...")
    print("="*80)

    best_shift = 0
    best_match = 0

    for shift in range(-10, 11):
        matches = 0
        for i in range(min_len):
            shifted_idx = i + shift
            if 0 <= shifted_idx < len(demodulated):
                if expected[i] == demodulated[shifted_idx]:
                    matches += 1

        if matches > best_match:
            best_match = matches
            best_shift = shift

    print(f"Best shift: {best_shift} bits → {best_match}/{min_len} matches ({best_match*100.0/min_len:.1f}%)")

    if abs(best_shift) > 0:
        print(f"⚠️  Signal appears shifted by {best_shift} bits")
        print("   Fix: Adjust timing recovery or symbol sync")

    # Show first errors in detail
    print("\n" + "="*80)
    print("FIRST 20 ERRORS (with context)")
    print("="*80)

    shown = 0
    for pos in error_positions[:20]:
        start = max(0, pos - 4)
        end = min(min_len, pos + 5)

        print(f"\nBit {pos}:")
        print(f"  Context: ", end="")
        for i in range(start, end):
            if i == pos:
                print(f"[{expected[i]}]", end="")
            else:
                print(expected[i], end="")
        print(f" (expected)")

        print(f"           ", end="")
        for i in range(start, end):
            if i == pos:
                print(f"[{demodulated[i]}]", end="")
            else:
                print(demodulated[i], end="")
        print(f" (received)")

        shown += 1

    print("\n" + "="*80)

    return errors, min_len

def main():
    print("\n" + "="*80)
    print("COSPAS-SARSAT 2G FRAME COMPARISON")
    print("="*80)

    # Build frames
    expected = build_expected_frame()
    demodulated = build_demodulated_frame()

    print(f"\nExpected frame length: {len(expected)} bits")
    print(f"  Preamble: {PREAMBLE_BITS} bits (alternating 0101...)")
    print(f"  Payload:  {len(expected) - PREAMBLE_BITS} bits (hex: {PAYLOAD_HEX})")

    print(f"\nDemodulated frame length: {len(demodulated)} bits")
    print(f"  Hex: {DEMODULATED_HEX}")

    # Compare
    errors, total = compare_frames(expected, demodulated)

    # Summary
    print("\n" + "="*80)
    print("SUMMARY")
    print("="*80)
    print(f"Bit Error Rate: {errors}/{total} = {errors*100.0/total:.2f}%")

    if errors > total * 0.9:
        print("\n🔴 CRITICAL: >90% errors → likely global inversion or wrong phase")
    elif errors > total * 0.5:
        print("\n⚠️  HIGH ERROR RATE: >50% errors → major synchronization issue")
    elif errors > total * 0.1:
        print("\n⚠️  Significant errors: check timing, phase, or PRN sync")
    else:
        print("\n✅ Low error rate: minor corrections needed")

    print("\n")

if __name__ == '__main__':
    main()
