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
#include "despread.h"    /* despread_sync_t */

typedef struct {
    float freq_hz;       /* estimated carrier offset */
    float confidence;    /* peak-to-mean correlation ratio */
    int   costas_phase;  /* best Costas phase (0..3 → 0°,90°,180°,270°) */
} freq_acq_result_t;

/**
 * @brief Sweep frequency offsets via PRN preamble correlation.
 *
 * Two-pass algorithm:
 *   Pass 1: 512-chip correlation, 100 Hz step, ±19 kHz range.
 *   Pass 2: 2048-chip correlation, 10 Hz step, ±150 Hz around coarse peak.
 *
 * @return 0 on success, -1 on error.
 */
int freq_acq_sweep(const float complex *chips, int n_chips,
                   float chip_rate,
                   float freq_min, float freq_max,
                   freq_acq_result_t *result);

/**
 * @brief Estimate residual frequency from despread-aligned preamble via FFT.
 *
 * Uses the sync result (off_I, off_Q, phase) from a first-pass despread
 * to extract correctly aligned preamble chips, strips the PRN modulation,
 * and runs an 8192-point FFT to find the residual carrier offset.
 *
 * This is more sensitive than freq_acq_sweep() at low SNR because it
 * leverages the full 6400-chip preamble at the correct alignment.
 *
 * @param chips       Chip-rate complex samples (post-decimation, pre-Costas).
 * @param n_chips     Available chip count.
 * @param sync        Alignment from despread_sync().
 * @param chip_rate   Chip rate in Hz.
 * @param result      Output: freq offset, confidence, phase.
 * @return 0 on success, -1 if sync invalid or no peak found.
 */
int freq_acq_from_alignment(const float complex *chips, int n_chips,
                            const despread_sync_t *sync,
                            float chip_rate,
                            freq_acq_result_t *result);

#endif /* FREQ_ACQ_H */
