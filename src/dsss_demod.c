/**
 * @file dsss_demod.c
 * @brief DSSS OQPSK receive chain — flat-chain rewrite (2026-05-22).
 *
 * No sample-rate tracking loop. The chain relies on:
 *   - freq_acq_fft_corr for carrier acquisition to ~1 Hz precision,
 *   - despread_sync for 4-phase Costas resolution via magnitude correlation,
 *   - despread_bits for residual frequency tracking via per-bit PLL (α=0.04, β=0.01).
 *
 * Chain:
 *   ota_buffer
 *     → DC blocker (IIR α=0.001)
 *     → boxcar decimation to chip rate (un-delayed, for acquisition only)
 *     → freq_acq_fft_corr (chip-rate FFT-correlation)
 *     → NCO wipeoff at sample rate
 *     → OQPSK delay (advance Q by SPS/2 — safe once carrier is wiped)
 *     → boxcar decimation to chip rate (final chip stream)
 *     → despread_burst → 250 bits
 *
 * The OQPSK delay must come AFTER the carrier wipeoff: if applied while
 * the residual carrier still rotates the constellation, the time-shift on
 * Q mixes I and Q contributions and destroys PRN correlation.
 */

#include "dsss_demod.h"
#include "despread.h"
#include "freq_acq.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Minimum confidence from freq_acq_fft_corr to accept the acquisition.
 * Empirically: real OTA locks give 40+, pure-noise picks give 2-4. */
#define ACQ_CONF_MIN 8.0f

/* In-place NCO carrier wipeoff at sample rate. */
static void nco_wipe(float complex *samples, size_t n,
                     float freq_hz, float fs)
{
    float dphi = -2.0f * (float)M_PI * freq_hz / fs;
    float step_r = cosf(dphi), step_i = sinf(dphi);
    float ph_r = 1.0f, ph_i = 0.0f;
    for (size_t k = 0; k < n; k++) {
        float re = __real__ samples[k];
        float im = __imag__ samples[k];
        __real__ samples[k] = re * ph_r - im * ph_i;
        __imag__ samples[k] = re * ph_i + im * ph_r;
        float nr = ph_r * step_r - ph_i * step_i;
        float ni = ph_r * step_i + ph_i * step_r;
        if ((k & 1023u) == 0u) {
            float inv = 1.0f / sqrtf(nr * nr + ni * ni);
            nr *= inv; ni *= inv;
        }
        ph_r = nr; ph_i = ni;
    }
}

/* Boxcar decimation: integrate isps samples per chip (matched filter for
 * the half-sine pulse shape). Robust to unknown sub-chip phase: worst
 * case -6 dB, no null (vs picking a single sample which can land on a
 * half-sine zero). */
static void boxcar_decimate(const float complex *in, size_t N,
                            int isps, float complex *out, size_t n_chips)
{
    for (size_t k = 0; k < n_chips; k++) {
        float complex acc = 0.0f;
        size_t base = k * (size_t)isps;
        for (int j = 0; j < isps; j++) {
            size_t idx = base + (size_t)j;
            if (idx < N) acc += in[idx];
        }
        out[k] = acc;
    }
}

int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       float sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits,
                       float *z_score)
{
    (void)max_doppler;

    if (!ota_buffer || !output_bits) return -1;
    if (sps < 4.0f || fs <= 0.0f) return -1;

    float chip_rate = fs / sps;
    if (fabsf(chip_rate - 38400.0f) > 100.0f) {
        fprintf(stderr, "[dsss_demod] chip rate %.0f Hz out of range\n",
                (double)chip_rate);
        return -1;
    }
    if (buffer_length < (size_t)fs) {
        fprintf(stderr, "[dsss_demod] buffer too short (%zu < %.0f samples)\n",
                buffer_length, (double)fs);
        return -1;
    }

    int isps = (int)(sps + 0.5f);
    size_t N = buffer_length;
    size_t n_chips = N / (size_t)isps;

    int rc = -1;
    float complex *work  = (float complex *)malloc(N * sizeof(float complex));
    float complex *chips = (float complex *)calloc(n_chips, sizeof(float complex));
    if (!work || !chips) {
        fprintf(stderr, "[dsss_demod] allocation failure\n");
        goto cleanup;
    }

    /* 1. DC blocker — removes IF/USB harmonics that pollute downstream FFTs. */
    memcpy(work, ota_buffer, N * sizeof(float complex));
    {
        float dc_i = 0.0f, dc_q = 0.0f;
        const float alpha = 0.001f;
        for (size_t t = 0; t < N; t++) {
            float r = __real__ work[t], i = __imag__ work[t];
            dc_i += alpha * (r - dc_i);
            dc_q += alpha * (i - dc_q);
            work[t] = (r - dc_i) + (i - dc_q) * I;
        }
    }

    /* 2. Coarse acquisition: boxcar to chip rate on the un-delayed,
     *    still-rotating signal. fft-corr internally rotates the chips at
     *    each test frequency to find the best (freq, lag, phase). */
    boxcar_decimate(work, N, isps, chips, n_chips);

    /* Cap n_chips at 12000 for acquisition: empirically (see archive
     * dsss_demod_20260522.c) freq_acq_fft_corr's last_lag = n_chips -
     * FFTC_COARSE_L grows with input, and at OTA SNR (10-13 dB) random
     * peaks at high lags beat the true preamble peak (typically lag
     * 600-1900 from historical locks). The 12000 cap matches the
     * 2026-05-19 working version that decoded the Toulouse CNES SGB.
     * Despread below still receives the full chip stream. */
    int n_chips_acq = (n_chips > 12000) ? 12000 : (int)n_chips;

    freq_acq_result_t acq;
    memset(&acq, 0, sizeof(acq));
    if (freq_acq_fft_corr(chips, n_chips_acq, chip_rate,
                          -8000.0f, 8000.0f, &acq) != 0 ||
        acq.confidence < ACQ_CONF_MIN) {
        fprintf(stderr,
                "[dsss_demod] acquisition rejected "
                "(freq=%.0f Hz conf=%.1f, need >=%.1f)\n",
                (double)acq.freq_hz, (double)acq.confidence,
                (double)ACQ_CONF_MIN);
        goto cleanup;
    }

    /* 3. NCO carrier wipeoff at sample rate on the full-rate buffer. */
    nco_wipe(work, N, acq.freq_hz, fs);

    /* 4. OQPSK delay: advance Q by SPS/2 (safe now that the carrier is wiped). */
    {
        int delay = isps / 2;
        for (size_t t = 0; t < N; t++) {
            float r = __real__ work[t];
            float q = (t + (size_t)delay < N)
                        ? __imag__ work[t + (size_t)delay]
                        : 0.0f;
            work[t] = r + q * I;
        }
    }

    /* 5. Final boxcar decimation to chip rate, on the wiped+delayed signal. */
    boxcar_decimate(work, N, isps, chips, n_chips);
    free(work);
    work = NULL;

    /* 6. Despread: sync (4-phase Costas via magnitude) + bits with bit-PLL. */
    rc = despread_burst(chips, (int)n_chips, output_bits, z_score);

cleanup:
    free(work);
    free(chips);
    return rc;
}
