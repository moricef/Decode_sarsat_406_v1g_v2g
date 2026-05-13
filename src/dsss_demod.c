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
    float chip_rate = fs / sps;
    size_t n_chips = 0;

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
    #define COARSE_FFT_MIN_CONF  5.0f
    #define COARSE_FFT_MIN_HZ     1.0f
    #define COARSE_FFT_MAX_HZ     25000.0f   /* Pluto 40 ppm + RTL-SDR 1 ppm ≈ 16.5 kHz */
    float coarse_freq_hz = 0.0f;
    {
        /* DC blocker BEFORE FFT — removes USB 150 Hz harmonics
         * that blind the 4th-power FFT. */
        memcpy(post_rrc, ota_buffer, N * sizeof(float complex));
        {
            float dc_i = 0.0f, dc_q = 0.0f;
            const float alpha = 0.001f;
            for (size_t t = 0; t < N; t++) {
                float ir = __real__ post_rrc[t];
                float qi = __imag__ post_rrc[t];
                dc_i += alpha * (ir - dc_i);
                dc_q += alpha * (qi - dc_q);
                post_rrc[t] = (ir - dc_i) + (qi - dc_q) * I;
            }
        }

        freq_acq_result_t acq;
        int acq_ok = 0;
        if (freq_acq_coarse_fft(post_rrc, (int)N, fs, chip_rate,
                                &acq) == 0
            && acq.confidence >= COARSE_FFT_MIN_CONF
            && fabsf(acq.freq_hz) >= COARSE_FFT_MIN_HZ
            && fabsf(acq.freq_hz) <= COARSE_FFT_MAX_HZ) {
            coarse_freq_hz = acq.freq_hz;
            acq_ok = 1;
        }

        /* Sweep fallback: PRN correlation at chip rate, ±300 Hz.
         * MUST decimate to chip rate first — sweep expects chip-rate data. */
        if (!acq_ok) {
            int phi = isps / 2;
            size_t n_chips_test = N / (size_t)isps;
            if (n_chips_test > 5000) n_chips_test = 5000;
            float complex *chip_test = (float complex *)calloc(n_chips_test, sizeof(float complex));
            if (chip_test) {
                for (size_t k = 0; k < n_chips_test; k++) {
                    size_t idx = (size_t)phi + k * (size_t)isps;
                    chip_test[k] = (idx < N) ? post_rrc[idx] : 0.0f;
                }
                freq_acq_result_t acq2;
                if (freq_acq_sweep(chip_test, (int)n_chips_test, chip_rate,
                                   -300.0f, 300.0f, &acq2) == 0
                    && acq2.confidence > 3.0f) {
                    coarse_freq_hz = acq2.freq_hz;
                    fprintf(stderr, "[freq_acq] sweep: %.0f Hz conf=%.1f\n",
                            (double)acq2.freq_hz, (double)acq2.confidence);
                }
                free(chip_test);
            }
        }

        /* OQPSK delay: advance Q channel by SPS/2 to undo TX Tc/2 offset. */
        int oqpsk_delay = (int)(sps / 2.0f + 0.5f);
        for (size_t t = 0; t < N; t++) {
            float ir = __real__ post_rrc[t];
            float qi = (t + (size_t)oqpsk_delay < N)
                       ? __imag__ post_rrc[t + (size_t)oqpsk_delay]
                       : 0.0f;
            delayed[t] = ir + I * qi;
        }
    }

    /* ---------------------------------------------------------------
     * 4. Pass-through (no matched filter for half-sine pulse shaping).
     * --------------------------------------------------------------- */
    memcpy(post_rrc, delayed, N * sizeof(float complex));

    /* ---------------------------------------------------------------
     * 5. Tracking loop (FLL+PLL+DLL+Kalman) — decimation + carrier
     *    tracking in a single sample-rate pass.
     * --------------------------------------------------------------- */
    n_chips = 0;
    {
        tracking_state_t trk;
        /* Fast code-phase scan: code NCO + carrier wipeoff + PRN corr.
         * No tracking_init needed — decimates 6400 chips at each of
         * 8 sub-chip offsets, picks the one maximizing correlation. */
        float init_code = 0.0f;
        {
            int8_t prn_buf[DESPREAD_PRN_LEN];
            despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PRN_LEN, prn_buf);
            float best_peak = 0.0f;
            int best_off = 0;
            for (int off = 0; off < 8; off++) {
                float sub = (float)off * 0.125f;
                /* Code NCO: code_phase starts at sub, first peak at sub+0.5 */
                float code_phase = sub;
                float code_freq = 1.0f / sps;
                /* Carrier NCO at coarse_freq_hz */
                float dphi = 2.0f * (float)M_PI * coarse_freq_hz / fs;
                float ph_r = 1.0f, ph_i = 0.0f;
                float step_r = cosf(dphi), step_i = sinf(dphi);
                int prev_peak = (int)(code_phase + 0.5f);
                int chip_idx = 0;
                float sum_i = 0.0f, sum_q = 0.0f;
                for (size_t s = 0; s < N && chip_idx < 6400; s++) {
                    code_phase += code_freq;
                    int cur_peak = (int)(code_phase + 0.5f);
                    if (cur_peak != prev_peak && cur_peak >= 0
                        && cur_peak < DESPREAD_PRN_LEN) {
                        /* Carrier wipeoff */
                        float re = __real__ post_rrc[s];
                        float im = __imag__ post_rrc[s];
                        float yr = re * ph_r + im * ph_i;
                        float yi = im * ph_r - re * ph_i;
                        float pr = 1.0f - 2.0f * (float)prn_buf[cur_peak];
                        sum_i += yr * pr;
                        sum_q += yi * pr;
                        chip_idx++;
                        prev_peak = cur_peak;
                    }
                    /* Advance carrier phasor */
                    float nr = ph_r * step_r - ph_i * step_i;
                    float ni = ph_r * step_i + ph_i * step_r;
                    if ((s & 1023u) == 0u) {
                        float inv = 1.0f / sqrtf(nr * nr + ni * ni);
                        nr *= inv; ni *= inv;
                    }
                    ph_r = nr; ph_i = ni;
                }
                float peak = sum_i * sum_i + sum_q * sum_q;
                if (peak > best_peak) { best_peak = peak; best_off = off; }
            }
            init_code = (float)best_off * 0.125f;
            fprintf(stderr, "[dsss_demod] scan: best=%.3f chip peak=%.0f\n",
                    (double)init_code, (double)best_peak);
        }

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

            /* Legacy fallback: fixed-phase decimation */
            int phi = isps / 2;
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

    /* ---------------------------------------------------------------
     * 6. Despread — unchanged.
     * --------------------------------------------------------------- */
    /* DEBUG: dump tracking output chips (first window only) */
    { static int once = 0;
      if (!once) { once = 1;
        FILE *f = fopen("/tmp/chips_tracking.bin", "wb");
        if (f) { fwrite(post_dec, sizeof(float complex), n_chips, f); fclose(f); } } }
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
