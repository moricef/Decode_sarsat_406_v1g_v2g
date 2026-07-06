/**
 * @file freq_acq.h
 * @brief DSSS frequency acquisition — coarse 4th-power FFT + PRN sweep fallback
 *
 * Primary: 4th-power FFT at sample rate (collapses OQPSK constellation),
 *          per standard DSP practice: coarse freq sync BEFORE timing sync.
 * Fallback: PRN-correlation sweep at chip rate (legacy).
 */

#ifndef FREQ_ACQ_H
#define FREQ_ACQ_H

#include <complex.h>
#include "despread.h"    /* despread_sync_t */

typedef struct {
    float freq_hz;       /* estimated carrier offset */
    float confidence;    /* peak-to-mean correlation ratio */
    int   costas_phase;  /* best Costas phase (0..3 → 0°,90°,180°,270°) */
    despread_prn_mode_t prn_mode; /* PRN mode that produced the best lock */
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
 * @brief Coarse frequency estimation via 4th-power FFT at sample rate.
 *
 * Raises baseband signal to 4th power, collapsing the OQPSK constellation
 * (chips are ±1±j, (±1±j)^4 = constant).  FFT of the result reveals a
 * tone at 4× the carrier offset.  Operates at sample rate BEFORE decimation
 * (coarse freq sync precedes timing sync — see PySDR Ch.14).
 *
 * The half-sine pulse shaping introduces a chip_rate/2 offset: the tone
 * appears at 4×(fo + chip_rate/2), not 4×fo.  This function corrects for it.
 *
 * @param samples     Raw I/Q samples at sample rate (pre-OQPSK delay).
 * @param n_samples   Number of samples available.
 * @param fs          Sample rate in Hz.
 * @param chip_rate   Chip rate in Hz.
 * @param result      Output: freq offset, confidence, phase (phase=0 always).
 * @return 0 on success, -1 on error.
 */
int freq_acq_coarse_fft(const float complex *samples, int n_samples,
                        float fs, float chip_rate,
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

/**
 * @brief FFT-correlation acquisition for weak DSSS bursts.
 *
 * Correlates the chip-rate input against the preamble PRN through the FFT:
 * one FFT pair yields the correlation at EVERY chip-lag, so chip alignment
 * is found for free and only carrier frequency is swept. The PRN
 * processing gain lifts a weak SGB clear of the noise where the 4th-power
 * FFT collapses.
 *
 * @param chips       Chip-rate complex samples.
 * @param n_chips     Available chip count.
 * @param chip_rate   Chip rate in Hz.
 * @param freq_min    Carrier-offset search range, low bound (Hz).
 * @param freq_max    Carrier-offset search range, high bound (Hz).
 * @param max_lag     Highest chip-lag to consider for the preamble peak
 *                    (0 = search the whole buffer). The preamble can only
 *                    sit inside the burst window's pre-roll, so capping the
 *                    lag search to that region stops spurious noise/data
 *                    correlations at higher lags from beating the true
 *                    preamble peak. The full n_chips buffer is still used
 *                    for the fine-frequency refinement.
 * @param result      Output: freq offset, confidence, phase.
 * @return 0 on success, -1 on error.
 */
int freq_acq_fft_corr(const float complex *chips, int n_chips,
                      float chip_rate,
                      float freq_min, float freq_max,
                      int max_lag,
                      freq_acq_result_t *result);

#endif /* FREQ_ACQ_H */
