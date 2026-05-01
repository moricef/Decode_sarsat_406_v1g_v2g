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

/* Coarse frequency sweep. */
#define COARSE_PREAMBLE_CHIPS 6400
#define COARSE_SWEEP_MIN_HZ  -25000.0f
#define COARSE_SWEEP_MAX_HZ   25000.0f
#define COARSE_SWEEP_STEP_HZ  400.0f
#define COARSE_MIN_CORR_PCT   0.65f  /* require ≥65% preamble match to trust */

#ifdef DSSS_DEBUG_DUMP
static void dump_complex(const char *path, const float complex *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(p, sizeof(float complex), n, f); fclose(f); }
}
#else
#define dump_complex(p, x, n) ((void)0)
#endif

/* -------- frequency sweep on preamble -----------------------------------
 *
 * Tests candidate frequency offsets by applying the correction to the
 * chip-rate samples, hard-slicing to {0,1}, then correlating the I-channel
 * preamble against NOT(PRN_I) — the same logic the despreader uses.
 * Returns the offset that maximises the chip match count.
 * ----------------------------------------------------------------------- */

static float coarse_freq_sweep(const float complex *chips, int n_chips,
                                float chip_rate)
{
    if (n_chips < COARSE_PREAMBLE_CHIPS || chips == NULL)
        return 0.0f;

    /* Generate NOT(PRN_I) for preamble correlation (phase 0°, data=0). */
    int8_t *npi = (int8_t *)malloc((size_t)COARSE_PREAMBLE_CHIPS);
    if (!npi) return 0.0f;
    despread_gen_prn(DESPREAD_PRN_SEED_I, COARSE_PREAMBLE_CHIPS, npi);
    for (int i = 0; i < COARSE_PREAMBLE_CHIPS; i++)
        npi[i] = (int8_t)(1 - npi[i]);  /* NOT(PRN_I) */

    float  *corrected_re = (float *)malloc((size_t)COARSE_PREAMBLE_CHIPS * sizeof(float));
    float  *corrected_im = (float *)malloc((size_t)COARSE_PREAMBLE_CHIPS * sizeof(float));
    if (!corrected_re || !corrected_im) {
        free(npi); free(corrected_re); free(corrected_im);
        return 0.0f;
    }

    int    best_score = 0;
    float  best_hz    = 0.0f;
    int    n_steps    = (int)((COARSE_SWEEP_MAX_HZ - COARSE_SWEEP_MIN_HZ)
                              / COARSE_SWEEP_STEP_HZ) + 1;

    for (int s = 0; s < n_steps; s++) {
        float f_hz = COARSE_SWEEP_MIN_HZ + (float)s * COARSE_SWEEP_STEP_HZ;

        /* Apply frequency correction: multiply by exp(-j*2π*f*k/chip_rate) */
        for (int k = 0; k < COARSE_PREAMBLE_CHIPS; k++) {
            float phase = -(M_TWOPI * f_hz * (float)k / chip_rate);
            float c = cosf(phase);
            float si = sinf(phase);
            float re = __real__ chips[k];
            float im = __imag__ chips[k];
            corrected_re[k] = re * c - im * si;
            corrected_im[k] = re * si + im * c;
        }

        /* Hard-slice I channel and correlate against NOT(PRN_I). */
        int score = 0;
        for (int k = 0; k < COARSE_PREAMBLE_CHIPS; k++) {
            int chip = (corrected_re[k] >= 0.0f) ? 1 : 0;
            if (chip == npi[k]) score++;
        }

        if (score > best_score) {
            best_score = score;
            best_hz    = f_hz;
        }
    }

    free(npi);
    free(corrected_re);
    free(corrected_im);

    float pct = (float)best_score / (float)COARSE_PREAMBLE_CHIPS;
    fprintf(stderr,
            "[dsss_demod] coarse sweep: best offset %.0f Hz, "
            "preamble corr %.1f%% (threshold %.0f%%)\n",
            (double)best_hz, (double)(100.0f * pct),
            (double)(100.0f * COARSE_MIN_CORR_PCT));

    if (pct < COARSE_MIN_CORR_PCT)
        return 0.0f;  /* sweep didn't find a credible tone → likely same clock */

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
    if (sps != DSSS_SPS) {
        fprintf(stderr, "[dsss_demod] sps=%d not supported (need %d)\n",
                sps, DSSS_SPS);
        return -1;
    }
    if (fs != (float)DSSS_SAMP_RATE_HZ) {
        fprintf(stderr, "[dsss_demod] fs=%.0f Hz not supported (need %d)\n",
                (double)fs, DSSS_SAMP_RATE_HZ);
        return -1;
    }
    /* Need at least one full burst for a sensible decode. */
    if (buffer_length < (size_t)DSSS_SAMP_RATE_HZ) {
        fprintf(stderr, "[dsss_demod] buffer too short (%zu < %d samples)\n",
                buffer_length, DSSS_SAMP_RATE_HZ);
        return -1;
    }

    int rc = -1;
    float *taps = NULL;
    float complex *delayed = NULL;
    float complex *post_rrc = NULL;
    float complex *post_dec = NULL;
    float complex *post_coarse = NULL;
    float complex *post_costas = NULL;

    /* ---------------------------------------------------------------
     * 1. Allocate per-stage buffers (offline, single big burst).
     * --------------------------------------------------------------- */
    size_t N = buffer_length;
    size_t chip_buf = N / (size_t)sps + 2;

    taps        = (float *)malloc(1024 * sizeof(float));
    delayed     = (float complex *)malloc(N * sizeof(float complex));
    post_rrc    = (float complex *)malloc(N * sizeof(float complex));
    post_dec    = (float complex *)malloc(chip_buf * sizeof(float complex));
    post_coarse = (float complex *)malloc(chip_buf * sizeof(float complex));
    post_costas = (float complex *)malloc(chip_buf * sizeof(float complex));
    if (!taps || !delayed || !post_rrc || !post_dec || !post_coarse || !post_costas) {
        fprintf(stderr, "[dsss_demod] allocation failure\n");
        goto cleanup;
    }

    /* ---------------------------------------------------------------
     * 2. Delay Q channel by SPS/2 samples (OQPSK alignment).
     *    delayed[t] = real(input[t]) + j * imag(input[t - 32])
     * --------------------------------------------------------------- */
    for (size_t t = 0; t < N; t++) {
        float ir = __real__ ota_buffer[t];
        float qi;
        if (t < (size_t)SGB_OQPSK_DELAY)
            qi = 0.0f;
        else
            qi = __imag__ ota_buffer[t - SGB_OQPSK_DELAY];
        delayed[t] = ir + I * qi;
    }
    dump_complex("/tmp/c_post_delay.bin", delayed, N);

    /* ---------------------------------------------------------------
     * 3. RRC matched filter (taps identical to GR firdes).
     * --------------------------------------------------------------- */
    int ntaps = rrc_compute_taps(SGB_RRC_GAIN,
                                 (float)DSSS_SAMP_RATE_HZ,
                                 (float)DSSS_CHIP_RATE,
                                 SGB_RRC_ALPHA,
                                 SGB_RRC_NTAPS_REQ,
                                 taps);
    if (ntaps <= 0) {
        fprintf(stderr, "[dsss_demod] rrc_compute_taps failed\n");
        goto cleanup;
    }
    rrc_filter_complex(delayed, N, taps, ntaps, post_rrc);
    dump_complex("/tmp/c_post_rrc.bin", post_rrc, N);

    /* ---------------------------------------------------------------
     * 4. Stage A symbol sync — find phase φ ∈ [0, sps) maximizing energy
     *    on the preamble, decimate by sps starting at φ.
     * --------------------------------------------------------------- */
    size_t n_chips = 0;
    int phi = symbol_sync_decimate(post_rrc, N, sps,
                                   25 * 256,   /* preamble chips per channel */
                                   post_dec, &n_chips);
    if (phi < 0 || n_chips < 38000) {
        fprintf(stderr,
                "[dsss_demod] symbol_sync failed (phi=%d, n_chips=%zu)\n",
                phi, n_chips);
        goto cleanup;
    }
    fprintf(stderr, "[dsss_demod] sync phi=%d, %zu chip samples\n", phi, n_chips);
    dump_complex("/tmp/c_post_decim.bin", post_dec, n_chips);

    /* ---------------------------------------------------------------
     * 5. Coarse frequency offset (sweep preamble correlation vs freq).
     * --------------------------------------------------------------- */
    {
        float coarse_hz = coarse_freq_sweep(post_dec, (int)n_chips,
                                             (float)DSSS_CHIP_RATE);
        if (fabsf(coarse_hz) > 1.0f) {
            fprintf(stderr, "[dsss_demod] coarse offset = %.0f Hz, correcting\n",
                    (double)coarse_hz);
        }
        /* Always copy (either corrected or pass-through) into post_coarse. */
        memcpy(post_coarse, post_dec, n_chips * sizeof(float complex));
        apply_freq_correction(post_coarse, (int)n_chips,
                               coarse_hz, (float)DSSS_CHIP_RATE);
        dump_complex("/tmp/c_post_coarse.bin", post_coarse, n_chips);
    }

    /* ---------------------------------------------------------------
     * 6. Costas loop (QPSK).
     * --------------------------------------------------------------- */
    costas4_t costas;
    costas4_init(&costas, SGB_COSTAS_BW);
    costas4_run(&costas, post_coarse, post_costas, n_chips);
    dump_complex("/tmp/c_post_costas.bin", post_costas, n_chips);

    /* ---------------------------------------------------------------
     * 7. Despread (slicer + sync + per-bit majority).
     * --------------------------------------------------------------- */
    if (despread_burst(post_costas, (int)n_chips, output_bits) != 0)
        goto cleanup;

    rc = 0;

cleanup:
    free(taps);
    free(delayed);
    free(post_rrc);
    free(post_dec);
    free(post_coarse);
    free(post_costas);
    return rc;
}
