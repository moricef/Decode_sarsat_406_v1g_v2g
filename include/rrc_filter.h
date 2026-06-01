/**
 * @file rrc_filter.h
 * @brief Root Raised Cosine matched filter (RRC)
 *
 * Port of GNU Radio's firdes.root_raised_cosine() (gr-filter/lib/firdes.cc).
 * Used as matched filter on the SARSAT 2G OQPSK signal generated with the
 * same RRC pulse-shape parameters (alpha=0.5, sps=64).
 */

#ifndef RRC_FILTER_H
#define RRC_FILTER_H

#include <stddef.h>
#include <complex.h>

/**
 * @brief Compute RRC filter taps (matches GR firdes.root_raised_cosine).
 *
 * Note: GR forces ntaps to be odd. If you pass an even ntaps, the actual
 * tap count returned is `ntaps | 1` (next odd). The caller's buffer must
 * be large enough.
 *
 * Normalization: DC gain. After scaling, sum(taps) = gain.
 *
 * @param gain      Filter gain (sum of taps after normalization).
 * @param samp_rate Sampling frequency in Hz.
 * @param sym_rate  Symbol/chip rate in Hz.
 * @param alpha     Roll-off factor (0..1).
 * @param ntaps     Requested tap count (forced odd).
 * @param taps      Output buffer, size >= (ntaps | 1).
 * @return Actual tap count produced (= ntaps | 1).
 */
int rrc_compute_taps(float gain, float samp_rate, float sym_rate,
                     float alpha, int ntaps, float *taps);

/**
 * @brief Apply RRC FIR filter to a complex signal (zero-padded edges).
 *
 * Output sample at index i is convolution centered at i (delay = ntaps/2).
 * For samples near the edges, missing input is treated as zero.
 *
 * @param input         Input complex samples.
 * @param input_length  Number of input samples.
 * @param taps          Filter taps (output of rrc_compute_taps).
 * @param ntaps         Number of taps.
 * @param output        Output buffer, length = input_length.
 */
void rrc_filter_complex(const float complex *input, size_t input_length,
                        const float *taps, int ntaps,
                        float complex *output);

#endif /* RRC_FILTER_H */
