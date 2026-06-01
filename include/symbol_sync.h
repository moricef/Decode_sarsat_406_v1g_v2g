/**
 * @file symbol_sync.h
 * @brief Symbol timing sync — Stage A (fixed-phase decimation)
 *
 * Stage A: signals from a clean modulator (no jitter, no Doppler, no
 * timing drift). Finds the optimal decimation phase by searching over
 * the SPS possible phases inside one chip period and picking the one
 * that maximizes energy on the preamble.
 *
 * Stage B (Mueller & Müller for OTA / Doppler) is deferred.
 */

#ifndef SYMBOL_SYNC_H
#define SYMBOL_SYNC_H

#include <stddef.h>
#include <complex.h>

/**
 * @brief Find the best decimation phase and decimate by sps.
 *
 * Searches phases phi in [0, sps). For each phi, computes the energy
 * of input[phi], input[phi+sps], ..., over `preamble_chips` chips. Picks
 * phi maximizing energy, then writes input[phi + k*sps] for k=0..K-1
 * (with K = (input_length - phi) / sps) to `output`.
 *
 * @param input          Complex signal post matched-filter, at sps samples/chip.
 * @param input_length   Number of input samples.
 * @param sps            Samples per chip (e.g. 64).
 * @param preamble_chips Chip count over which to estimate energy.
 * @param output         Output buffer, length >= input_length / sps + 1.
 * @param output_length  [out] Number of decimated samples written.
 * @return Best phase found in [0, sps), or -1 on error.
 */
int symbol_sync_decimate(const float complex *input, size_t input_length,
                         int sps, int preamble_chips,
                         float complex *output, size_t *output_length);

#endif /* SYMBOL_SYNC_H */
