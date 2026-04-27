/**
 * @file dsss_demod.c
 * @brief Pure-C DSSS OQPSK receive chain (façade).
 *
 * Replaces the broken MATLAB Coder wrapper. Orchestrates:
 *
 *   1. Q-channel delay by SPS/2 samples (undo OQPSK Tc/2 offset →
 *      I and Q chip transitions land on integer chip boundaries).
 *   2. RRC matched filter (firdes.root_raised_cosine, α=0.5, 11×SPS taps).
 *   3. Stage A symbol sync — pick best decimation phase by max energy on
 *      the preamble (no jitter / no Doppler assumption for v1).
 *   4. Costas loop (QPSK, BW=0.0628) for residual phase correction.
 *   5. Despreader (T.018 PRN seeds, 2-pass I/Q sync, per-bit majority).
 *
 * On success, writes 250 bits to output_bits[] suitable for decode_2g().
 *
 * Optional: build with -DDSSS_DEBUG_DUMP to write per-stage binary dumps
 * to /tmp/c_*.bin for diff against the GR pipeline.
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

/* Receive-chain parameters (mirror the validated GR flowgraph). */
#define SGB_RRC_NTAPS_REQ    (11 * DSSS_SPS)   /* 704; rrc forces odd → 705 */
#define SGB_RRC_ALPHA        0.5f
#define SGB_RRC_GAIN         1.0f
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
    post_costas = (float complex *)malloc(chip_buf * sizeof(float complex));
    if (!taps || !delayed || !post_rrc || !post_dec || !post_costas) {
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
     * 5. Costas loop (QPSK).
     * --------------------------------------------------------------- */
    costas4_t costas;
    costas4_init(&costas, SGB_COSTAS_BW);
    costas4_run(&costas, post_dec, post_costas, n_chips);
    dump_complex("/tmp/c_post_costas.bin", post_costas, n_chips);

    /* ---------------------------------------------------------------
     * 6. Despread (slicer + sync + per-bit majority).
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
