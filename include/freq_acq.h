/**
 * @file freq_acq.h
 * @brief DSSS PRN-correlation frequency acquisition
 *
 * Two-pass hierarchical sweep replacing the coarse FFT. Operates at chip rate
 * (38.4 kHz), correlating partial preamble against expected PRN to find the
 * LO offset with ~5 Hz accuracy.
 */

#ifndef FREQ_ACQ_H
#define FREQ_ACQ_H

#include <complex.h>

typedef struct {
    float freq_hz;       /* estimated carrier offset */
    float confidence;    /* peak-to-mean correlation ratio */
    int   costas_phase;  /* best Costas phase (0..3 → 0°,90°,180°,270°) */
} freq_acq_result_t;

/**
 * @brief Sweep frequency offsets via PRN preamble correlation.
 *
 * Two-pass algorithm:
 *   Pass 1: 256-chip correlation, 100 Hz step, ±30 kHz range.
 *   Pass 2: 2048-chip correlation, 10 Hz step, ±150 Hz around coarse peak.
 *
 * Uses a rotating phasor for NCO (no trig in inner loop).  The 4 Costas
 * phase ambiguities are tried at each frequency.  Three coarse chip offsets
 * (0, SPS/2, SPS) are checked to handle burst-alignment uncertainty.
 *
 * @param chips       Chip-rate complex samples (post-decimation).
 * @param n_chips     Available chip count (≥ 6400 for preamble).
 * @param chip_rate   Chip rate in Hz (typ. 38400).
 * @param freq_min    Minimum search frequency (typ. -30000).
 * @param freq_max    Maximum search frequency (typ. +30000).
 * @param result      Output: best offset, confidence, phase.
 * @return 0 on success, -1 on error.
 */
int freq_acq_sweep(const float complex *chips, int n_chips,
                   float chip_rate,
                   float freq_min, float freq_max,
                   freq_acq_result_t *result);

#endif /* FREQ_ACQ_H */
