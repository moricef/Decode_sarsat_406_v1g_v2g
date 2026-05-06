/**
 * @file dsss_demod.c
 * @brief Pure-C DSSS OQPSK receive chain.
 *
 * Orchestrates (per Zhang et al. ICEE 2025, Fig. 59.1):
 *   1. freq_acq_coarse_fft() — 4th-power FFT at sample rate.
 *   2. Q-channel advance by SPS/2 (undo OQPSK Tc/2 delay).
 *   3. DC blocker (IIR, alpha=0.001).
 *   4. Tracking loop (FLL+PLL+DLL+Kalman) — coarse freq feeds carrier NCO.
 *   5. Despreader (T.018 PRN seeds, 2-pass I/Q sync, per-bit majority).
 *
 * On success, writes 250 bits to output_bits[] suitable for decode_2g().
 */

#include "dsss_demod.h"
#include "symbol_sync.h"
#include "costas4.h"
#include "despread.h"
#include "freq_acq.h"
#include "tracking.h"

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

#define DSSS_DEBUG_DUMP 1
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
                       uint8_t *output_bits,
                       float *z_score)
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
    (void)isps; /* used only in legacy fallback path */
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
     * 2. Coarse frequency acquisition via 4th-power FFT at sample rate.
     *    MUST precede decimation: at chip rate (Nyquist), any offset
     *    aliases and becomes unrecoverable.  See PySDR Ch.14.
     *
     *    The coarse estimate is passed to tracking_init() — the tracking
     *    loop's own carrier NCO handles the wipe-off (Zhang et al. Fig. 59.1).
     *    No separate NCO correction stage here (was redundant).
     * --------------------------------------------------------------- */
    #define COARSE_FFT_MIN_CONF  100.0f
    #define COARSE_FFT_MIN_HZ     1.0f
    float coarse_freq_hz = 0.0f;
    {
        freq_acq_result_t acq;
        if (freq_acq_coarse_fft(ota_buffer, (int)N, fs, chip_rate,
                                &acq) == 0
            && acq.confidence >= COARSE_FFT_MIN_CONF
            && fabsf(acq.freq_hz) >= COARSE_FFT_MIN_HZ) {
            coarse_freq_hz = acq.freq_hz;
        }
        /* Samples pass through — tracking_init NCO handles the wipe-off. */
        memcpy(post_rrc, ota_buffer, N * sizeof(float complex));
    }

    /* ---------------------------------------------------------------
     * 3. OQPSK delay: advance Q channel by SPS/2 to undo TX Tc/2 offset.
     * --------------------------------------------------------------- */
    int oqpsk_delay = (int)(sps / 2.0f + 0.5f);
    for (size_t t = 0; t < N; t++) {
        float ir = __real__ post_rrc[t];
        float qi = (t + (size_t)oqpsk_delay < N)
                   ? __imag__ post_rrc[t + (size_t)oqpsk_delay]
                   : 0.0f;
        delayed[t] = ir + I * qi;
    }
    /* DC blocker — essential for RTL-SDR (strong carrier leak). */
    {
        float dc_i = 0.0f, dc_q = 0.0f;
        const float alpha = 0.001f;
        for (size_t t = 0; t < N; t++) {
            float ir = __real__ delayed[t];
            float qi = __imag__ delayed[t];
            dc_i += alpha * (ir - dc_i);
            dc_q += alpha * (qi - dc_q);
            delayed[t] = (ir - dc_i) + (qi - dc_q) * I;
        }
    }
    dump_complex("/tmp/c_post_delay.bin", delayed, N);

    /* ---------------------------------------------------------------
     * 4. Pass-through (no matched filter for half-sine pulse shaping).
     * --------------------------------------------------------------- */
    memcpy(post_rrc, delayed, N * sizeof(float complex));

    /* ---------------------------------------------------------------
     * 5. Tracking loop (FLL+PLL+DLL+Kalman) — decimation + carrier
     *    tracking in a single sample-rate pass.
     *
     *    Phase 0: pass-through (legacy fixed-phase decimation).
     *    Phases 1-5: see tracking.c for progressive implementation.
     * --------------------------------------------------------------- */
    size_t n_chips = 0;
    {
        tracking_state_t trk;
        float init_code = 0.0f;

        if (tracking_init(&trk, fs, sps, coarse_freq_hz, init_code) != 0) {
            fprintf(stderr, "[dsss_demod] tracking_init failed\n");
            goto cleanup;
        }

        size_t n_chips_tracked = 0;
        int trk_rc = tracking_run(&trk, post_rrc, N,
                                  post_dec, &n_chips_tracked);
        tracking_free(&trk);

        if (trk_rc != 0 || n_chips_tracked < 38000) {
            fprintf(stderr,
                    "[dsss_demod] tracking failed (%zu chips), "
                    "falling back to legacy\n", n_chips_tracked);

            /* Legacy fallback: fixed-phase decimation + Costas bypass */
            int phi = isps / 2;
            n_chips = 0;
            for (size_t k = 0; k < N / (size_t)isps && n_chips < chip_buf; k++) {
                size_t idx = (size_t)phi + k * (size_t)isps;
                if (idx < N) post_dec[n_chips++] = post_rrc[idx];
            }
            if (n_chips < 38000) {
                fprintf(stderr,
                        "[dsss_demod] legacy decimation also failed "
                        "(phi=%d, n_chips=%zu)\n", phi, n_chips);
                goto cleanup;
            }
        } else {
            n_chips = n_chips_tracked;
        }
    }

    dump_complex("/tmp/c_post_decim.bin", post_dec, n_chips);

    /* ---------------------------------------------------------------
     * 6. Despread — unchanged.
     * --------------------------------------------------------------- */
    if (despread_burst(post_dec, (int)n_chips, output_bits, z_score) != 0)
        goto cleanup;

    rc = 0;

cleanup:
    free(delayed);
    free(post_rrc);
    free(post_dec);
    free(post_costas);
    return rc;
}
