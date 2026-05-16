/**
 * @file despread.h
 * @brief T.018 SGB DSSS despreader — port of decode_sgb_epy_block_0.py
 *
 * Inputs: complex chip-rate stream from the Costas loop output.
 *         (Internally sliced as: chip_I = (Re >= 0) ? 1 : 0,
 *                                 chip_Q = (Im >= 0) ? 1 : 0.)
 * Output: 250 message bits interleaved I[0],Q[0],I[1],Q[1],...,I[124],Q[124]
 *
 * Algorithm (soft-correlation, validated against GR Python despreader):
 *   1. Generate PRN_I (seed 0x000001) and PRN_Q (seed 0x1AC1FC), each 38400 chips.
 *   2. Sync — 2 passes with soft complex correlation:
 *        Pass A: search offset_I in [0,200) and Costas phase in {0,90,180,270}
 *                maximizing dot(chip_I, expected_I) over 6400 preamble chips.
 *        Pass B: with phase fixed, search offset_Q in [offset_I-5, offset_I+5]
 *                maximizing dot(chip_Q, expected_Q) over 6400 preamble chips.
 *      Threshold: peak-to-mean correlation ratio >= 2.5.
 *   3. Despread bits 25..149: per-bit soft correlation over 256 chips,
 *      decision by sign of the accumulated dot product.
 */

#ifndef DESPREAD_H
#define DESPREAD_H

#include <stdint.h>
#include <stddef.h>
#include <complex.h>

/* T.018 SGB constants */
#define DESPREAD_PRN_LEN          38400
#define DESPREAD_PRN_SEED_I       0x000001UL
#define DESPREAD_PRN_SEED_Q       0x1AC1FCUL
#define DESPREAD_CHIPS_PER_BIT    256
#define DESPREAD_PREAMBLE_BITS    25       /* per channel */
#define DESPREAD_MSG_BITS         125      /* per channel */
#define DESPREAD_TOTAL_BITS       150      /* per channel */
#define DESPREAD_PREAMBLE_CHIPS   (DESPREAD_PREAMBLE_BITS * DESPREAD_CHIPS_PER_BIT)
/* Preamble search span: one scan step (0.25 s = 9600 chips), so the
 * window catching a burst at its smallest offset always finds it.
 * The tracking chip-emission cap and the despread search both scale
 * off this value. */
#define DESPREAD_SYNC_RANGE       9600
#define DESPREAD_SYNC_THRESHOLD   2.8f   /* combined z-score sqrt(z_i²+z_q²) */
#define DESPREAD_OUTPUT_BITS      250      /* 125 I + 125 Q interleaved */

/**
 * @brief Generate a 23-bit LFSR PRN sequence (T.018, polynomial x^23 + x^18 + 1).
 *
 * State update: fb = (state ^ (state >> 18)) & 1; state = (state >> 1) | (fb << 22);
 * Output bit per step: state & 1 (BEFORE the shift).
 *
 * @param seed    Initial state (bits 0..22 used).
 * @param length  Chips to generate.
 * @param out     Buffer of `length` int8 chips, each in {0, 1}.
 */
void despread_gen_prn(uint32_t seed, int length, int8_t *out);

/**
 * @brief Get expected I-channel preamble chips (all-zero bits → raw PRN).
 *
 * Generates the first n chips of the T.018 preamble (despread seed I,
 * data bits = 0 → no inversion).  Output is ±1 per chip, suitable for
 * dot-product correlation.
 *
 * @param out   Output buffer, length n.
 * @param n     Number of preamble chips to generate.
 */
void despread_get_preamble_chips(int *out, int n);

/**
 * @brief Preamble sync result.
 *
 * off_i/off_q  : chip offset for I/Q channels.
 * phase        : Costas ambiguity 0..3.
 * score_i/q    : soft-correlation peak-to-mean ratio × 100
 *                (e.g. 350 = 3.5× above mean).
 */
typedef struct {
    int off_i;   /* chip offset for I channel */
    int off_q;   /* chip offset for Q channel */
    int phase;   /* Costas ambiguity 0..3 */
    int score_i; /* peak-to-mean ratio × 10 */
    int score_q; /* peak-to-mean ratio × 10 */
    float z_comb; /* combined z-score (sqrt(z_i² + z_q²)) */
} despread_sync_t;

/**
 * @brief Preamble sync: find off_i, off_q, phase from preamble chips.
 *
 * @return 0 on success, -1 if no sync above threshold.
 */
int despread_sync(const float complex *samples, int num_chips,
                  despread_sync_t *sync);

/**
 * @brief Despread message bits using pre-computed sync parameters.
 *
 * @param sync   Sync result from despread_sync().
 * @return 0 on success (250 bits written), -1 on error.
 */
int despread_bits(const float complex *samples, int num_chips,
                  const despread_sync_t *sync,
                  uint8_t *output_bits);

/**
 * @brief Despread a chip-rate complex stream into 250 message bits.
 *
 * Convenience wrapper: calls despread_sync() then despread_bits().
 *
 * @param samples       Chip-rate complex samples (Costas output).
 * @param num_chips     Number of chip samples available.
 * @param output_bits   250 bits, one per uint8_t (value 0 or 1).
 * @return 0 on success, -1 if sync failed.
 */
int despread_burst(const float complex *samples, int num_chips,
                   uint8_t *output_bits, float *z_score);

#endif /* DESPREAD_H */
