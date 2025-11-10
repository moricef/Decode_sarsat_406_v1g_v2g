/**
 * @file prn_generator.h
 * @brief T.018 PRN Generator (LFSR x^23 + x^18 + 1)
 *
 * Generates pseudorandom sequences for DSSS spreading.
 * Compliant with T.018 Rev.12 Table 2.2 and Appendix D.
 */

#ifndef PRN_GENERATOR_H
#define PRN_GENERATOR_H

#include <stdint.h>

// T.018 LFSR parameters
#define PRN_LFSR_LENGTH     23          // LFSR register length
#define PRN_CHIPS_PER_BIT   256         // Spreading factor

// T.018 Table 2.2 initial states
#define PRN_INIT_NORMAL_I   0x000001    // Normal mode I-channel: bit 0 = 1
#define PRN_INIT_NORMAL_Q   0x000041    // Normal mode Q-channel: offset 64 chips
#define PRN_INIT_TEST_I     0x69E780    // Self-test mode I-channel
#define PRN_INIT_TEST_Q     0x3CB948    // Self-test mode Q-channel

// PRN generator state
typedef struct {
    uint32_t lfsr_i;        // I-channel LFSR state (23 bits)
    uint32_t lfsr_q;        // Q-channel LFSR state (23 bits)
    uint8_t mode;           // 0=Normal, 1=Self-test
} prn_state_t;

/**
 * @brief Initialize PRN generator
 * @param state PRN state structure
 * @param mode 0=Normal, 1=Self-test
 */
void prn_init(prn_state_t *state, uint8_t mode);

/**
 * @brief Generate I-channel PRN sequence (256 chips)
 * @param state PRN state structure
 * @param sequence Output buffer (256 chips: +1 or -1)
 */
void prn_generate_i(prn_state_t *state, int8_t *sequence);

/**
 * @brief Generate Q-channel PRN sequence (256 chips)
 * @param state PRN state structure
 * @param sequence Output buffer (256 chips: +1 or -1)
 */
void prn_generate_q(prn_state_t *state, int8_t *sequence);

/**
 * @brief Verify PRN generator against T.018 Table 2.2
 * @return 1 if valid, 0 if mismatch
 *
 * Expected first 64 chips (Normal I): 8000 0108 4212 84A1
 */
uint8_t prn_verify_table_2_2(void);

#endif // PRN_GENERATOR_H
