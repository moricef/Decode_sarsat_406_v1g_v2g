/**
 * @file costas4.h
 * @brief 4th-order Costas loop for QPSK carrier recovery
 *
 * Port of GNU Radio's costas_loop_cc (order=4) + control_loop.
 * Same per-sample state and same numerical sequence as GR so a
 * bit-for-bit replay is possible if needed.
 */

#ifndef COSTAS4_H
#define COSTAS4_H

#include <stddef.h>
#include <complex.h>

typedef struct {
    float phase;       /* d_phase */
    float freq;        /* d_freq */
    float damping;     /* d_damping = sqrt(2)/2 */
    float loop_bw;     /* d_loop_bw */
    float alpha;       /* d_alpha (computed) */
    float beta;        /* d_beta  (computed) */
    float max_freq;    /* d_max_freq, default +1.0 */
    float min_freq;    /* d_min_freq, default -1.0 */
} costas4_t;

/**
 * @brief Initialize Costas loop with the same defaults as GR costas_loop_cc.
 *
 * d_damping = sqrt(2)/2, max_freq = +1, min_freq = -1.
 *
 * @param st       State to initialize.
 * @param loop_bw  Loop bandwidth (rad/sample).
 */
void costas4_init(costas4_t *st, float loop_bw);

/**
 * @brief Run the Costas loop on a complex stream.
 *
 * For each input sample:
 *   1. y = x * exp(-j*phase)         (NCO mixer)
 *   2. err = sign(Re(y))*Im(y) - sign(Im(y))*Re(y)   (phase_detector_4)
 *   3. err = branchless_clip(err, 1.0)
 *   4. freq  += beta * err
 *      phase += freq + alpha * err
 *      phase_wrap to [-2*pi, 2*pi]
 *      frequency_limit to [-max_freq, max_freq]
 *   5. output = y
 *
 * @param st     State (mutated).
 * @param in     Input complex samples.
 * @param out    Output complex samples (length n).
 * @param n      Number of samples to process.
 */
void costas4_run(costas4_t *st, const float complex *in,
                 float complex *out, size_t n);

#endif /* COSTAS4_H */
