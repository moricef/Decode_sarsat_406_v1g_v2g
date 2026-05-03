/**
 * @file dsss_demod.c
 * @brief Pure-C DSSS OQPSK receive chain (façade).
 *
 * Orchestrates:
 *   1. Q-channel delay by SPS/2 (undo OQPSK Tc/2 offset).
 *   2. Decimation to chip rate at energy-max phase.
 *   3. freq_acq_sweep() — PRN-correlation frequency sweep.
 *   4. Costas loop (QPSK, BW=0.0628) for fine phase correction.
 *   5. Despreader (T.018 PRN seeds, 2-pass I/Q sync, per-bit majority).
 *
 * On success, writes 250 bits to output_bits[] suitable for decode_2g().
 */

#include "dsss_demod.h"
#include "symbol_sync.h"
#include "costas4.h"
#include "despread.h"
#include "freq_acq.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Receive-chain parameters. */
#define SGB_COSTAS_BW        0.0628f
#define SGB_OQPSK_DELAY      (DSSS_SPS / 2)    /* 32 samples */

#ifdef DSSS_DEBUG_DUMP
static void dump_complex(const char *path, const float complex *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, sizeof(float complex), n, f); fclose(f); }
}
#else
#define dump_complex(p, x, n) ((void)0)
#endif

int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       float sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits)
{
    (void)max_doppler;  /* reserved for Stage B (M&M / Doppler search) */

    if (ota_buffer == NULL || output_bits == NULL)
        return -1;
    if (sps < 4.0f || fs <= 0.0f) {
        fprintf(stderr, "[dsss_demod] invalid sps=%.3f or fs=%.0f\n",
                (double)sps, (double)fs);
        return -1;
    }
    {
        float chip_rate = fs / sps;
        if (fabsf(chip_rate - 38400.0f) > 100.0f) {
            fprintf(stderr,
                    "[dsss_demod] chip rate %.0f Hz out of range "
                    "(need ~38400 Hz). fs=%.0f sps=%.3f\n",
                    (double)chip_rate, (double)fs, (double)sps);
            return -1;
        }
    }
    if (buffer_length < (size_t)fs) {
        fprintf(stderr, "[dsss_demod] buffer too short (%zu < %.0f samples)\n",
                buffer_length, (double)fs);
        return -1;
    }

    int rc = -1;
    float complex *delayed = NULL;
    float complex *post_rrc = NULL;
    float complex *post_dec = NULL;
    float complex *post_costas = NULL;

    int isps = (int)(sps + 0.5f);
    int exact_sps = (fabsf(sps - (float)isps) < 0.001f);
    float chip_rate = fs / sps;

    /* ---------------------------------------------------------------
     * 1. Allocate per-stage buffers.
     * --------------------------------------------------------------- */
    size_t N = buffer_length;
    size_t chip_buf = (size_t)((float)N / sps) + DESPREAD_SYNC_RANGE + DESPREAD_CHIPS_PER_BIT;

    delayed     = (float complex *)malloc(N * sizeof(float complex));
    post_rrc    = (float complex *)malloc(N * sizeof(float complex));
    post_dec    = (float complex *)calloc(chip_buf, sizeof(float complex));
    post_costas = (float complex *)calloc(chip_buf, sizeof(float complex));
    if (!delayed || !post_rrc || !post_dec || !post_costas) {
        fprintf(stderr, "[dsss_demod] allocation failure\n");
        goto cleanup;
    }

    /* ---------------------------------------------------------------
     * 2. Advance Q channel by SPS/2 samples to undo TX OQPSK Tc/2 delay.
     * --------------------------------------------------------------- */
    int oqpsk_delay = (int)(sps / 2.0f + 0.5f);
    for (size_t t = 0; t < N; t++) {
        float ir = __real__ ota_buffer[t];
        float qi = (t + (size_t)oqpsk_delay < N)
                   ? __imag__ ota_buffer[t + (size_t)oqpsk_delay]
                   : 0.0f;
        delayed[t] = ir + I * qi;
    }
    dump_complex("/tmp/c_post_delay.bin", delayed, N);

    /* ---------------------------------------------------------------
     * 3. Pass-through (no matched filter for half-sine pulse shaping).
     * --------------------------------------------------------------- */
    memcpy(post_rrc, delayed, N * sizeof(float complex));

    /* ---------------------------------------------------------------
     * 4. Decimate to chip rate at energy-max phase.
     * --------------------------------------------------------------- */
    size_t n_chips = 0;
    int phi;

    if (exact_sps) {
        phi = symbol_sync_decimate(post_rrc, N, isps,
                                   25 * 256,
                                   post_dec, &n_chips);
    } else {
        float best_phi_f = 0.0f;
        float best_energy = -1e30f;
        for (float pi = 0.0f; pi < sps; pi += 0.5f) {
            float pos = pi;
            size_t cnt = 0;
            float energy = 0.0f;
            while (pos < (float)N - sps && cnt < (size_t)(25 * 256)) {
                size_t idx = (size_t)pos;
                float frac = pos - (float)idx;
                float complex v = post_rrc[idx] * (1.0f - frac)
                                + post_rrc[idx + 1] * frac;
                float re = __real__ v, im = __imag__ v;
                energy += re * re + im * im;
                pos += sps;
                cnt++;
            }
            if (cnt < (size_t)(25 * 256)) continue;
            if (energy > best_energy) {
                best_energy = energy;
                best_phi_f = pi;
            }
        }
        phi = (int)(best_phi_f + 0.5f);
        float pos = best_phi_f;
        while (pos < (float)N - sps && n_chips < chip_buf) {
            size_t idx = (size_t)pos;
            float frac = pos - (float)idx;
            post_dec[n_chips++] = post_rrc[idx] * (1.0f - frac)
                                + post_rrc[idx + 1] * frac;
            pos += sps;
        }
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
     * 5. Frequency acquisition via PRN-correlation sweep at chip rate,
     *    then NCO correction at sample rate, re-OQPSK delay, re-decimate.
     *
     *    Only triggers if confidence >= 3.0 (avoids false positives on
     *    noise-only buffers).  The Costas loop handles small residuals.
     * --------------------------------------------------------------- */
    #define FREQ_ACQ_MIN_CONF  2.5f
    #define FREQ_ACQ_SWEEP_HZ  19000.0f  /* chip-rate Nyquist */
    int freq_was_corrected = 0;
    {
        freq_acq_result_t acq;
        if (freq_acq_sweep(post_dec, (int)n_chips, chip_rate,
                           -FREQ_ACQ_SWEEP_HZ, FREQ_ACQ_SWEEP_HZ, &acq) == 0
            && acq.confidence >= FREQ_ACQ_MIN_CONF) {
            freq_was_corrected = 1;

            /* NCO correction at sample rate on raw ota_buffer copy. */
            {
                float dphi = 2.0f * (float)M_PI * acq.freq_hz / fs;
                float step_r = cosf(dphi), step_i = -sinf(dphi);
                float ph_r = 1.0f, ph_i = 0.0f;
                for (size_t k = 0; k < N; k++) {
                    float re = __real__ ota_buffer[k];
                    float im = __imag__ ota_buffer[k];
                    post_rrc[k] = (re * ph_r - im * ph_i)
                                + (re * ph_i + im * ph_r) * I;
                    float nr = ph_r * step_r - ph_i * step_i;
                    float ni = ph_r * step_i + ph_i * step_r;
                    if ((k & 1023u) == 0u) {
                        float inv = 1.0f / sqrtf(nr * nr + ni * ni);
                        nr *= inv; ni *= inv;
                    }
                    ph_r = nr; ph_i = ni;
                }
            }
            /* Re-do OQPSK delay from corrected ota. */
            for (size_t t = 0; t < N; t++) {
                float ir = __real__ post_rrc[t];
                float qi = (t + (size_t)oqpsk_delay < N)
                           ? __imag__ post_rrc[t + (size_t)oqpsk_delay]
                           : 0.0f;
                delayed[t] = ir + I * qi;
            }
            memcpy(post_rrc, delayed, N * sizeof(float complex));
            /* Re-decimate at energy-max phase. */
            n_chips = 0;
            if (exact_sps) {
                phi = symbol_sync_decimate(post_rrc, N, isps,
                                           25 * 256,
                                           post_dec, &n_chips);
            } else {
                float best_phi_f = 0.0f;
                float best_energy = -1e30f;
                for (float pi = 0.0f; pi < sps; pi += 0.5f) {
                    float pos = pi;
                    size_t cnt = 0;
                    float energy = 0.0f;
                    while (pos < (float)N - sps && cnt < (size_t)(25 * 256)) {
                        size_t idx = (size_t)pos;
                        float frac = pos - (float)idx;
                        float complex v = post_rrc[idx] * (1.0f - frac)
                                        + post_rrc[idx + 1] * frac;
                        float re = __real__ v, im = __imag__ v;
                        energy += re * re + im * im;
                        pos += sps;
                        cnt++;
                    }
                    if (cnt < (size_t)(25 * 256)) continue;
                    if (energy > best_energy) {
                        best_energy = energy;
                        best_phi_f = pi;
                    }
                }
                phi = (int)(best_phi_f + 0.5f);
                float pos = best_phi_f;
                while (pos < (float)N - sps && n_chips < chip_buf) {
                    size_t idx = (size_t)pos;
                    float frac = pos - (float)idx;
                    post_dec[n_chips++] = post_rrc[idx] * (1.0f - frac)
                                        + post_rrc[idx + 1] * frac;
                    pos += sps;
                }
            }
            if (phi < 0 || n_chips < 38000) {
                fprintf(stderr,
                        "[dsss_demod] re-decimation failed (phi=%d, n_chips=%zu)\n",
                        phi, n_chips);
                goto cleanup;
            }
            fprintf(stderr, "[dsss_demod] re-decim phi=%d, %zu chip samples\n",
                    phi, n_chips);
        }
    }

    /* ---------------------------------------------------------------
     * 6. Costas loop — always runs for fine phase tracking.
     * --------------------------------------------------------------- */
    {
        costas4_t costas;
        costas4_init(&costas, SGB_COSTAS_BW);
        costas4_run(&costas, post_dec, post_costas, n_chips);
    }
    dump_complex("/tmp/c_post_costas.bin", post_costas, n_chips);

    /* ---------------------------------------------------------------
     * 7. Despread, optionally with alignment-guided freq refinement.
     *
     *    If freq_acq_sweep already corrected the offset, despread
     *    directly.  Otherwise, use despread_sync to find alignment,
     *    then freq_acq_from_alignment() to check for residual offset
     *    via FFT on the aligned preamble; if found, apply NCO at
     *    sample rate and re-do the chain.  Otherwise despread with
     *    the found sync alignment.
     * --------------------------------------------------------------- */
    if (freq_was_corrected) {
        if (despread_burst(post_costas, (int)chip_buf, output_bits) != 0)
            goto cleanup;
    } else {
        despread_sync_t sync;
        if (despread_sync(post_costas, (int)n_chips, &sync) != 0)
            goto cleanup;
        freq_acq_result_t acq2;
        if (freq_acq_from_alignment(post_dec, (int)n_chips, &sync,
                                    chip_rate, &acq2) == 0
            && fabsf(acq2.freq_hz) > 1.0f
            && acq2.confidence >= 3.0f) {
            /* Offset found via alignment FFT — apply NCO at sample rate. */
            {
                float dphi = 2.0f * (float)M_PI * acq2.freq_hz / fs;
                float step_r = cosf(dphi), step_i = -sinf(dphi);
                float ph_r = 1.0f, ph_i = 0.0f;
                for (size_t k = 0; k < N; k++) {
                    float re = __real__ ota_buffer[k];
                    float im = __imag__ ota_buffer[k];
                    post_rrc[k] = (re * ph_r - im * ph_i)
                                + (re * ph_i + im * ph_r) * I;
                    float nr = ph_r * step_r - ph_i * step_i;
                    float ni = ph_r * step_i + ph_i * step_r;
                    if ((k & 1023u) == 0u) {
                        float inv = 1.0f / sqrtf(nr * nr + ni * ni);
                        nr *= inv; ni *= inv;
                    }
                    ph_r = nr; ph_i = ni;
                }
            }
            for (size_t t = 0; t < N; t++) {
                float ir = __real__ post_rrc[t];
                float qi = (t + (size_t)oqpsk_delay < N)
                           ? __imag__ post_rrc[t + (size_t)oqpsk_delay]
                           : 0.0f;
                delayed[t] = ir + I * qi;
            }
            memcpy(post_rrc, delayed, N * sizeof(float complex));
            n_chips = 0;
            if (exact_sps) {
                phi = symbol_sync_decimate(post_rrc, N, isps,
                                           25 * 256,
                                           post_dec, &n_chips);
            } else {
                float pos = (float)phi;
                while (pos < (float)N - sps && n_chips < chip_buf) {
                    size_t idx = (size_t)pos;
                    float frac = pos - (float)idx;
                    post_dec[n_chips++] = post_rrc[idx] * (1.0f - frac)
                                        + post_rrc[idx + 1] * frac;
                    pos += sps;
                }
            }
            if (n_chips < 38000) goto cleanup;
            {
                costas4_t costas;
                costas4_init(&costas, SGB_COSTAS_BW);
                costas4_run(&costas, post_dec, post_costas, n_chips);
            }
            if (despread_burst(post_costas, (int)chip_buf, output_bits) != 0)
                goto cleanup;
        } else {
            if (despread_bits(post_costas, (int)n_chips,
                              &sync, output_bits) != 0)
                goto cleanup;
        }
    }

    rc = 0;

cleanup:
    free(delayed);
    free(post_rrc);
    free(post_dec);
    free(post_costas);
    return rc;
}
