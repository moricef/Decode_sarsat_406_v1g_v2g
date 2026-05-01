/**
 * @file dsss_demod.c
 * @brief Pure-C DSSS OQPSK receive chain (façade).
 *
 * Orchestrates:
 *   1. Q-channel delay by SPS/2 (undo OQPSK Tc/2 offset).
 *   2. RRC matched filter (firdes.root_raised_cosine, α=0.5, 11×SPS taps).
 *   3. Stage A symbol sync — decimation phase by max energy on preamble.
 *   4. Coarse frequency sweep — preamble correlation vs freq offset
 *      to pull the residual within Costas lock range (~±300 Hz).
 *   5. Costas loop (QPSK, BW=0.0628) for fine phase correction.
 *   6. Despreader (T.018 PRN seeds, 2-pass I/Q sync, per-bit majority).
 *
 * On success, writes 250 bits to output_bits[] suitable for decode_2g().
 */

#include "dsss_demod.h"
#include "rrc_filter.h"
#include "symbol_sync.h"
#include "costas4.h"
#include "despread.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_TWOPI (2.0 * M_PI)

/* Receive-chain parameters. */
#define SGB_RRC_NTAPS_REQ    (11 * DSSS_SPS)   /* 704; rrc forces odd → 705 */
#define SGB_RRC_ALPHA        0.5f
#define SGB_RRC_GAIN         1.0f
#define SGB_COSTAS_BW        0.0628f
#define SGB_OQPSK_DELAY      (DSSS_SPS / 2)    /* 32 samples */

/* Coarse frequency estimator (FFT on modulation-stripped preamble). */
#define COARSE_FFT_N           8192   /* radix-2, ~4.7 Hz bin at 38.4 kHz chip rate */
#define COARSE_PREAMBLE_CHIPS  6400   /* 25 bits × 256 chips */
#define COARSE_PEAK_THRESH     15.0f  /* peak-to-mean ratio for valid detection */

#ifdef DSSS_DEBUG_DUMP
static void dump_complex(const char *path, const float complex *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, sizeof(float complex), n, f); fclose(f); }
}
#else
#define dump_complex(p, x, n) ((void)0)
#endif

/* -------- radix-2 FFT (in-place, float complex) --------------------------- */
static void fft_radix2(float complex *x, int n, int invert)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float complex t = x[i]; x[i] = x[j]; x[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float a = (float)(M_TWOPI / (double)len) * (invert ? -1.0f : 1.0f);
        float complex wlen = cosf(a) - sinf(a) * I;
        for (int i = 0; i < n; i += len) {
            float complex w = 1.0f;
            for (int j = 0; j < len / 2; j++) {
                float complex u = x[i + j];
                float complex v = x[i + j + len / 2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (invert) {
        float inv = 1.0f / (float)n;
        for (int i = 0; i < n; i++) x[i] *= inv;
    }
}

/* -------- coarse frequency estimator (FFT on stripped preamble) ---------
 *
 * For each decimation phase and each Costas phase, multiplies the received
 * preamble chips by the conjugate of the expected (±1 ± j) pattern, which
 * strips the DSSS modulation.  An FFT on the residue reveals a tone at the
 * carrier offset with ~4.7 Hz resolution.
 * ----------------------------------------------------------------------- */

static float coarse_freq_fft(const float complex *chips, int n_chips,
                              float chip_rate,
                              const float complex *raw,
                              size_t raw_len, int sps,
                              int *out_phi)
{
    if (raw == NULL || raw_len < (size_t)(COARSE_PREAMBLE_CHIPS * sps))
        return 0.0f;
    (void)chips; (void)n_chips;

    int8_t *prn_i = (int8_t *)malloc((size_t)COARSE_PREAMBLE_CHIPS);
    int8_t *prn_q = (int8_t *)malloc((size_t)COARSE_PREAMBLE_CHIPS);
    if (!prn_i || !prn_q) { free(prn_i); free(prn_q); return 0.0f; }
    despread_gen_prn(DESPREAD_PRN_SEED_I, COARSE_PREAMBLE_CHIPS, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, COARSE_PREAMBLE_CHIPS, prn_q);

    float complex *fft_buf = (float complex *)malloc(
        (size_t)COARSE_FFT_N * sizeof(float complex));
    float complex *phase_chips = (float complex *)malloc(
        (size_t)COARSE_PREAMBLE_CHIPS * sizeof(float complex));
    if (!fft_buf || !phase_chips) {
        free(prn_i); free(prn_q); free(fft_buf); free(phase_chips);
        return 0.0f;
    }

    float best_hz    = 0.0f;
    float best_ratio = 0.0f;
    int   best_phi   = 0;

    for (int pi = 0; pi < sps; pi++) {
        size_t n_avail = 0;
        for (size_t i = (size_t)pi;
             i < raw_len && n_avail < (size_t)COARSE_PREAMBLE_CHIPS;
             i += (size_t)sps)
            phase_chips[n_avail++] = raw[i];

        if (n_avail < (size_t)COARSE_PREAMBLE_CHIPS)
            continue;

        for (int p = 0; p < 4; p++) {
            float complex dc_sum = 0.0f;
            for (int k = 0; k < COARSE_FFT_N; k++) {
                if (k < COARSE_PREAMBLE_CHIPS) {
                    float ie, qe;
                    switch (p) {
                    case 0: ie = 1.0f-2.0f*prn_i[k]; qe = 1.0f-2.0f*prn_q[k]; break;
                    case 1: ie = 1.0f-2.0f*prn_q[k]; qe = -(1.0f-2.0f*prn_i[k]); break;
                    case 2: ie = -(1.0f-2.0f*prn_i[k]); qe = -(1.0f-2.0f*prn_q[k]); break;
                    default:ie = -(1.0f-2.0f*prn_q[k]); qe = 1.0f-2.0f*prn_i[k]; break;
                    }
                    fft_buf[k] = phase_chips[k] * (ie - qe * I);
                    dc_sum += fft_buf[k];
                } else {
                    fft_buf[k] = 0.0f;
                }
            }

            float complex dc_mean = dc_sum / (float)COARSE_PREAMBLE_CHIPS;
            for (int k = 0; k < COARSE_PREAMBLE_CHIPS; k++)
                fft_buf[k] -= dc_mean;

            fft_radix2(fft_buf, COARSE_FFT_N, 0);
            for (int i = 0; i < COARSE_FFT_N; i++) {
                float re = __real__ fft_buf[i], im = __imag__ fft_buf[i];
                __real__ fft_buf[i] = re * re + im * im;
            }

            float peak_mag = 0.0f;
            int   peak_bin = 0;
            float noise_sum = 0.0f;
            int   noise_cnt = 0;
            int   dc_guard = 30;

            for (int i = 0; i < COARSE_FFT_N / 2; i++) {
                float mag = __real__ fft_buf[i];
                if (i >= dc_guard) {
                    if (mag > peak_mag) { peak_mag = mag; peak_bin = i; }
                    noise_sum += mag; noise_cnt++;
                }
            }
            float noise_mean = noise_cnt > 0 ? noise_sum / (float)noise_cnt : 1e-10f;
            float ratio = peak_mag / noise_mean;

            /* Quadratic interpolation for sub-bin accuracy. */
            float bin_f = (float)peak_bin;
            if (peak_bin > dc_guard && peak_bin < COARSE_FFT_N / 2 - 1) {
                float L = __real__ fft_buf[peak_bin - 1];
                float C = peak_mag;
                float R = __real__ fft_buf[peak_bin + 1];
                float denom = 2.0f * C - L - R;
                if (denom > 1e-10f)
                    bin_f += 0.5f * (R - L) / denom;
            }
            float est_hz = bin_f * chip_rate / (float)COARSE_FFT_N;

            if (ratio > best_ratio) {
                best_ratio = ratio;
                best_hz = est_hz;
                best_phi = pi;
            }
        }
    }

    free(prn_i); free(prn_q); free(fft_buf); free(phase_chips);

    fprintf(stderr,
            "[dsss_demod] coarse fft: phi=%d, offset %.0f Hz, "
            "peak/noise %.1f (thresh %.1f)\n",
            best_phi, (double)best_hz, (double)best_ratio,
            (double)COARSE_PEAK_THRESH);

    if (best_ratio < COARSE_PEAK_THRESH)
        return 0.0f;

    *out_phi = best_phi;
    return best_hz;
}

/* -------- apply frequency correction to chip-rate buffer --------------- */

static void apply_freq_correction(float complex *samples, int n,
                                   float offset_hz, float chip_rate)
{
    if (fabsf(offset_hz) < 1.0f || samples == NULL || n < 1)
        return;

    for (int k = 0; k < n; k++) {
        float phase = -(M_TWOPI * offset_hz * (float)k / chip_rate);
        float c = cosf(phase);
        float s = sinf(phase);
        float re = __real__ samples[k];
        float im = __imag__ samples[k];
        samples[k] = (re * c - im * s) + (re * s + im * c) * I;
    }
}

int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits)
{
    (void)max_doppler;  /* reserved for Stage B (M&M / Doppler search) */

    if (ota_buffer == NULL || output_bits == NULL)
        return -1;
    if (sps < 4 || fs <= 0.0f) {
        fprintf(stderr, "[dsss_demod] invalid sps=%d or fs=%.0f\n",
                sps, (double)fs);
        return -1;
    }
    {
        float chip_rate = fs / (float)sps;
        if (fabsf(chip_rate - 38400.0f) > 100.0f) {
            fprintf(stderr,
                    "[dsss_demod] chip rate %.0f Hz out of range "
                    "(need ~38400 Hz). fs=%.0f sps=%d\n",
                    (double)chip_rate, (double)fs, sps);
            return -1;
        }
    }
    /* Need at least one full burst for a sensible decode (~1 s). */
    if (buffer_length < (size_t)fs) {
        fprintf(stderr, "[dsss_demod] buffer too short (%zu < %.0f samples)\n",
                buffer_length, (double)fs);
        return -1;
    }

    int rc = -1;
    float *taps = NULL;
    float complex *delayed = NULL;
    float complex *post_rrc = NULL;
    float complex *post_dec = NULL;
    float complex *post_costas = NULL;

    /* ---------------------------------------------------------------
     * 1. Allocate per-stage buffers.
     * --------------------------------------------------------------- */
    size_t N = buffer_length;
    size_t chip_buf = N / (size_t)sps + 2;

    taps        = (float *)malloc((size_t)(11 * sps + 2) * sizeof(float));
    delayed     = (float complex *)malloc(N * sizeof(float complex));
    post_rrc    = (float complex *)malloc(N * sizeof(float complex));
    post_dec    = (float complex *)malloc(chip_buf * sizeof(float complex));
    post_costas = (float complex *)malloc(chip_buf * sizeof(float complex));
    if (!taps || !delayed || !post_rrc || !post_dec || !post_costas) {
        fprintf(stderr, "[dsss_demod] allocation failure\n");
        goto cleanup;
    }

    /* ---------------------------------------------------------------
     * 2. Delay Q channel by SPS/2 samples (OQPSK alignment).
     * --------------------------------------------------------------- */
    int oqpsk_delay = sps / 2;
    for (size_t t = 0; t < N; t++) {
        float ir = __real__ ota_buffer[t];
        float qi;
        if (t < (size_t)oqpsk_delay)
            qi = 0.0f;
        else
            qi = __imag__ ota_buffer[t - (size_t)oqpsk_delay];
        delayed[t] = ir + I * qi;
    }
    dump_complex("/tmp/c_post_delay.bin", delayed, N);

    /* ---------------------------------------------------------------
     * 3. Coarse frequency/phase estimation (FFT on raw delayed signal).
     * --------------------------------------------------------------- */
    int   coarse_phi = -1;
    float coarse_hz  = 0.0f;
    {
        float chip_rate = fs / (float)sps;
        coarse_hz = coarse_freq_fft(NULL, 0, chip_rate,
                                     delayed, N, sps, &coarse_phi);
        if (fabsf(coarse_hz) > 1.0f) {
            fprintf(stderr,
                    "[dsss_demod] coarse offset = %.0f Hz, "
                    "raw phi=%d, correcting before RRC\n",
                    (double)coarse_hz, coarse_phi);
            apply_freq_correction(delayed, (int)N, coarse_hz, (float)fs);
        }
    }

    /* ---------------------------------------------------------------
     * 4. RRC matched filter (on frequency-corrected signal).
     * --------------------------------------------------------------- */
    {
        float chip_rate = fs / (float)sps;
        int ntaps = rrc_compute_taps(SGB_RRC_GAIN,
                                     fs,
                                     chip_rate,
                                     SGB_RRC_ALPHA,
                                     11 * sps,
                                     taps);
    if (ntaps <= 0) {
        fprintf(stderr, "[dsss_demod] rrc_compute_taps failed\n");
        goto cleanup;
    }
    rrc_filter_complex(delayed, N, taps, ntaps, post_rrc);
    dump_complex("/tmp/c_post_rrc.bin", post_rrc, N);
    }

    /* ---------------------------------------------------------------
     * 5. Decimate at the best phase, Costas, despread.
     * --------------------------------------------------------------- */
    size_t n_chips = 0;
    int phi;
    if (coarse_phi >= 0) {
        /* Coarse FFT found the best decimation phase on the raw signal.
         * The RRC filter is centred (zero group delay), so the same
         * phase works on the post-RRC signal. */
        phi = coarse_phi;
        n_chips = 0;
        for (size_t i = (size_t)phi; i < N; i += (size_t)sps)
            post_dec[n_chips++] = post_rrc[i];
    } else {
        phi = symbol_sync_decimate(post_rrc, N, sps,
                                   25 * 256,
                                   post_dec, &n_chips);
    }
    if (phi < 0 || n_chips < 38000) {
        fprintf(stderr,
                "[dsss_demod] decimation failed (phi=%d, n_chips=%zu)\n",
                phi, n_chips);
        goto cleanup;
    }
    fprintf(stderr, "[dsss_demod] decim phi=%d, %zu chip samples\n", phi, n_chips);
    dump_complex("/tmp/c_post_decim.bin", post_dec, n_chips);

    /* ---------------------------------------------------------------
     * 6. Costas / pass-through.
     *
     *    When coarse FFT corrected an offset the residual is ~5 Hz —
     *    the Costas acquisition transient (several ms) smears the
     *    166 ms preamble more than the residual drift does.  We feed
     *    the decimated chips directly to the despreader.
     *
     *    When no coarse correction was applied the Costas runs as usual.
     * --------------------------------------------------------------- */
    if (fabsf(coarse_hz) > 1.0f) {
        memcpy(post_costas, post_dec, n_chips * sizeof(float complex));
    } else {
        costas4_t costas;
        costas4_init(&costas, SGB_COSTAS_BW);
        costas4_run(&costas, post_dec, post_costas, n_chips);
    }
    dump_complex("/tmp/c_post_costas.bin", post_costas, n_chips);

    /* ---------------------------------------------------------------
     * 7. Despread.
     * --------------------------------------------------------------- */
    if (despread_burst(post_costas, (int)n_chips, output_bits) != 0)
        goto cleanup;

    rc = 0;

cleanup:
    free(taps);
    free(delayed);
    free(post_rrc);
    free(post_dec);
    free(post_costas);
    return rc;
}
