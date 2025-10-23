/**
 * @file prn_generator.c
 * @brief T.018 PRN Generator Implementation
 *
 * LFSR: x^23 + x^18 + 1 (Fibonacci configuration)
 * Feedback: X0 ⊕ X18 (validated against T.018 Table 2.2)
 * Shift: RIGHT
 */

#include "prn_generator.h"
#include <stdio.h>
#include <string.h>

void prn_init(prn_state_t *state, uint8_t mode) {
    if (mode == 0) {
        // Normal mode (T.018 Table 2.2)
        state->lfsr_i = PRN_INIT_NORMAL_I;
        state->lfsr_q = PRN_INIT_NORMAL_Q;
    } else {
        // Self-test mode
        state->lfsr_i = PRN_INIT_TEST_I;
        state->lfsr_q = PRN_INIT_TEST_Q;
    }
    state->mode = mode;
}

void prn_generate_i(prn_state_t *state, int8_t *sequence) {
    uint32_t lfsr = state->lfsr_i;
    
    for (int i = 0; i < PRN_CHIPS_PER_BIT; i++) {
        // Output = X0 (LSB), T.018 Table 2.3: Logic 1 → -1, Logic 0 → +1
        sequence[i] = (lfsr & 1) ? -1 : +1;
        
        // LFSR feedback: X0 ⊕ X18 (T.018 Appendix D Figure D-1)
        uint8_t feedback = (lfsr ^ (lfsr >> 18)) & 1;
        
        // Shift RIGHT, feedback into X22
        lfsr = (lfsr >> 1) | ((uint32_t)feedback << 22);
        
        // Mask 23 bits
        lfsr &= 0x7FFFFF;
    }
    
    state->lfsr_i = lfsr;
}

void prn_generate_q(prn_state_t *state, int8_t *sequence) {
    uint32_t lfsr = state->lfsr_q;
    
    for (int i = 0; i < PRN_CHIPS_PER_BIT; i++) {
        // Output = X0 (LSB), T.018 Table 2.3: Logic 1 → -1, Logic 0 → +1
        sequence[i] = (lfsr & 1) ? -1 : +1;
        
        // LFSR feedback: X0 ⊕ X18 (same as I-channel)
        uint8_t feedback = (lfsr ^ (lfsr >> 18)) & 1;
        
        // Shift RIGHT, feedback into X22
        lfsr = (lfsr >> 1) | ((uint32_t)feedback << 22);
        
        // Mask 23 bits
        lfsr &= 0x7FFFFF;
    }
    
    state->lfsr_q = lfsr;
}

uint8_t prn_verify_table_2_2(void) {
    // T.018 Table 2.2 reference (Normal I, first 64 chips)
    // Hex: 8000 0108 4212 84A1
    // Logic bit → signal level: 1 → -1, 0 → +1
    const int8_t reference[64] = {
        -1,+1,+1,+1,+1,+1,+1,+1, +1,+1,+1,+1,+1,+1,+1,+1, // 8000
        +1,+1,+1,+1,+1,+1,+1,-1, +1,+1,+1,+1,-1,+1,+1,+1, // 0108
        +1,-1,+1,+1,+1,+1,-1,+1, +1,+1,+1,-1,+1,+1,-1,+1, // 4212
        -1,+1,+1,+1,+1,-1,+1,+1, -1,+1,-1,+1,+1,+1,+1,-1  // 84A1
    };
    
    // Generate first 64 chips
    uint32_t lfsr = PRN_INIT_NORMAL_I;
    int8_t test_seq[64];
    
    for (int i = 0; i < 64; i++) {
        test_seq[i] = (lfsr & 1) ? -1 : +1;
        uint8_t feedback = (lfsr ^ (lfsr >> 18)) & 1;
        lfsr = (lfsr >> 1) | ((uint32_t)feedback << 22);
        lfsr &= 0x7FFFFF;
    }
    
    // Verify
    for (int i = 0; i < 64; i++) {
        if (test_seq[i] != reference[i]) {
            fprintf(stderr, "PRN ERROR: Chip %d mismatch (got %d, expected %d)\n",
                    i, test_seq[i], reference[i]);
            return 0;
        }
    }
    
    printf("✓ PRN LFSR validated (T.018 Table 2.2: 8000 0108 4212 84A1)\n");
    return 1;
}
