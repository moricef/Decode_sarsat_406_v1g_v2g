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
#include "diag_log.h"

/* Provided by dec406_v2g.c */
extern int bch_decode_250_202(const uint8_t *msg, uint8_t *out, uint8_t *cw_out);
extern int bch_decode_250_202_nerr(const uint8_t *msg, uint8_t *out,
                                   int *n_errors);

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

/* Boxcar decimation: integrate isps samples per chip, starting at
 * sample offset 'offset' within each chip period. */
static void boxcar_decimate_off(const float complex *in, size_t N,
                                int isps, int offset,
                                float complex *out, size_t n_chips)
{
    for (size_t k = 0; k < n_chips; k++) {
        float complex acc = 0.0f;
        size_t base = k * (size_t)isps + (size_t)offset;
        for (int j = 0; j < isps; j++) {
            size_t idx = base + (size_t)j;
            if (idx < N) acc += in[idx];
        }
        out[k] = acc;
    }
}

static void boxcar_decimate(const float complex *in, size_t N,
                            int isps, float complex *out, size_t n_chips)
{
    boxcar_decimate_off(in, N, isps, 0, out, n_chips);
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
        DIAG("[dsss_demod] chip rate %.0f Hz out of range\n",
                (double)chip_rate);
        return -1;
    }
    if (buffer_length < (size_t)fs) {
        DIAG("[dsss_demod] buffer too short (%zu < %.0f samples)\n",
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
        DIAG("[dsss_demod] allocation failure\n");
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

    float acq_conf_min = ACQ_CONF_MIN;
    { const char *e = getenv("ACQ_CONF_MIN");
      if (e) acq_conf_min = (float)atof(e); }

    freq_acq_result_t acq;
    memset(&acq, 0, sizeof(acq));
    if (freq_acq_fft_corr(chips, n_chips_acq, chip_rate,
                          -8000.0f, 8000.0f, &acq) != 0 ||
        acq.confidence < acq_conf_min) {
        DIAG("[dsss_demod] acquisition rejected "
             "(freq=%.0f Hz conf=%.1f, need >=%.1f)\n",
             (double)acq.freq_hz, (double)acq.confidence,
             (double)acq_conf_min);
        goto cleanup;
    }

    /* 3a. Frequency refinement against despread_sync's narrow preamble response.
     * freq_acq maximises its own correlation, whose optimum sits 5-7 Hz off
     * despread_sync's. The preamble response is sharp (~+/-10 Hz to half-max,
     * zero beyond +/-15 Hz), so that offset can collapse sync (z=0) even with
     * high freq_acq confidence. The refinement must mirror the real despread
     * path (wipeoff + OQPSK + decimate), so process the buffer once at
     * acq.freq, then probe +/-15 Hz by chip-rate derotation (the OQPSK-phase
     * error across that span is ~1e-3 rad, negligible). */
    {
        float complex *tmp = (float complex *)malloc(N * sizeof(float complex));
        float complex *c0   = (float complex *)malloc(n_chips * sizeof(float complex));
        float complex *dr   = (float complex *)malloc(n_chips * sizeof(float complex));
        if (tmp && c0 && dr) {
            memcpy(tmp, work, N * sizeof(float complex));
            nco_wipe(tmp, N, acq.freq_hz, fs);
            if (getenv("NO_OQPSK") == NULL) {
                int delay = isps / 2;
                for (size_t t = 0; t < N; t++) {
                    float r = __real__ tmp[t];
                    float q = (t + (size_t)delay < N)
                                ? __imag__ tmp[t + (size_t)delay] : 0.0f;
                    tmp[t] = r + q * I;
                }
            }
            boxcar_decimate(tmp, N, isps, c0, n_chips);

            float best_f = acq.freq_hz, best_z = -1.0f;
            for (int df = -15; df <= 15; df += 5) {
                float dphi = -2.0f * (float)M_PI * (float)df / chip_rate;
                float pr = 1.0f, pi = 0.0f;
                float sr = cosf(dphi), si = sinf(dphi);
                for (size_t k = 0; k < n_chips; k++) {
                    float re = __real__ c0[k], im = __imag__ c0[k];
                    dr[k] = (re * pr - im * pi) + (re * pi + im * pr) * I;
                    float nr = pr * sr - pi * si, ni = pr * si + pi * sr;
                    pr = nr; pi = ni;
                }
                despread_sync_t s;
                despread_sync(dr, (int)n_chips, &s);
                if (s.z_comb > best_z) { best_z = s.z_comb;
                                         best_f = acq.freq_hz + (float)df; }
            }
            if (best_f != acq.freq_hz)
                DIAG("[dsss_demod] freq refined %.0f -> %.0f Hz (z=%.1f)\n",
                     (double)acq.freq_hz, (double)best_f, (double)best_z);
            acq.freq_hz = best_f;
        }
        free(tmp); free(c0); free(dr);
    }

    /* 3. NCO carrier wipeoff at sample rate on the full-rate buffer. */
    nco_wipe(work, N, acq.freq_hz, fs);

    /* 4. OQPSK delay: advance Q by SPS/2 (safe now that the carrier is wiped).
     * Diagnostic toggle NO_OQPSK=1 bypasses this to test whether the fixed
     * half-chip shift mismatches the burst's sub-chip phase. */
    if (getenv("NO_OQPSK") == NULL) {
        int delay = isps / 2;
        for (size_t t = 0; t < N; t++) {
            float r = __real__ work[t];
            float q = (t + (size_t)delay < N)
                        ? __imag__ work[t + (size_t)delay]
                        : 0.0f;
            work[t] = r + q * I;
        }
    }

    /* 5-6. Multi-offset boxcar + despread + BCH oracle.
     *
     *    The boxcar decimation phase relative to TX chip boundaries is
     *    unknown.  Try 4 sub-chip offsets (0, isps/4, isps/2, 3isps/4)
     *    and for each, run 4-phase Costas rotation through BCH.
     *    Keep the first (offset, phase) pair that BCH-validates.
     *    Cost: up to 4×4 = 16 despread_bits calls (~200 ms total). */
    {
        uint8_t bch_tmp[202];
        int best_offset = -1, best_phase = -1;
        float best_z = 0.0f;
        static const int n_offsets = 4;
        int offsets[4] = { 0, isps / 4, isps / 2, 3 * isps / 4 };

        int best_nerr = 99;
        for (int oi = 0; oi < n_offsets; oi++) {
            size_t n_chips_off = (N - (size_t)offsets[oi]) / (size_t)isps;
            if (n_chips_off < 6400) continue;

            boxcar_decimate_off(work, N, isps, offsets[oi],
                                chips, n_chips_off);

            despread_sync_t sync;
            if (despread_sync(chips, (int)n_chips_off, &sync) != 0)
                continue;

            if (oi == 0 && z_score)
                *z_score = sync.z_comb;
            if (sync.z_comb > best_z)
                best_z = sync.z_comb;

            int original_phase = sync.phase;
            for (int p = 0; p < 4; p++) {
                sync.phase = p;
                if (despread_bits(chips, (int)n_chips_off, &sync,
                                 NULL, NULL, output_bits) != 0)
                    continue;
                int nerr = -1;
                int brc = bch_decode_250_202_nerr(output_bits,
                                                   bch_tmp, &nerr);
                DIAG("[dsss_demod] offset %d/%d phase %d° "
                     "z=%.0f nerr=%d\n",
                     offsets[oi], isps, p * 90,
                     (double)sync.z_comb, nerr);
                if (nerr < best_nerr) best_nerr = nerr;
                if (brc == 0) {
                    best_offset = oi;
                    best_phase = p;
                    break;
                }
            }
            if (best_offset >= 0) break;

            sync.phase = original_phase;
        }

        if (z_score && best_z > *z_score)
            *z_score = best_z;

        if (best_offset >= 0) {
            DIAG("[dsss_demod] BCH validated: boxcar offset %d/%d "
                 "Costas phase %d° nerr=%d\n",
                 offsets[best_offset], isps, best_phase * 90, best_nerr);
            rc = 0;
        } else {
            DIAG("[dsss_demod] BCH FAIL all 16 combos, best nerr=%d\n",
                 best_nerr);
            /* Fallback: re-decimate at offset 0, return best-effort bits. */
            boxcar_decimate(work, N, isps, chips, n_chips);
            despread_sync_t sync;
            if (despread_sync(chips, (int)n_chips, &sync) == 0) {
                rc = despread_bits(chips, (int)n_chips, &sync,
                                  NULL, NULL, output_bits);
                if (z_score) *z_score = sync.z_comb;

                /* E/P/L sub-chip diagnostic on sample-rate buffer.
                 * For each bit, compute boxcar+PRN correlation at
                 * 3 sample offsets: Early (-isps/4), Prompt (0),
                 * Late (+isps/4).  Diagnoses chip timing drift. */
                {
                    static FILE *epl_csv = NULL;
                    static int epl_burst = 0;
                    if (!epl_csv) {
                        epl_csv = fopen("epl_diag.csv", "w");
                        if (epl_csv)
                            fprintf(epl_csv,
                                    "burst,bit,cs_i,E,P,L,pwr\n");
                    }
                    if (epl_csv) {
                        epl_burst++;
                        int8_t *prn_i = (int8_t *)malloc(DESPREAD_PRN_LEN);
                        int8_t *prn_q = (int8_t *)malloc(DESPREAD_PRN_LEN);
                        if (prn_i && prn_q) {
                            despread_gen_prn(DESPREAD_PRN_SEED_I,
                                             DESPREAD_PRN_LEN, prn_i);
                            despread_gen_prn(DESPREAD_PRN_SEED_Q,
                                             DESPREAD_PRN_LEN, prn_q);
                            int epl_off[3] = { -isps / 4, 0, isps / 4 };
                            for (int k = 0; k < DESPREAD_TOTAL_BITS; k++) {
                                int cs_i = sync.off_i * isps
                                           + k * DESPREAD_CHIPS_PER_BIT * isps;
                                float epl[3];
                                for (int ei = 0; ei < 3; ei++) {
                                    float acc_re = 0, acc_im = 0;
                                    for (int c = 0; c < DESPREAD_CHIPS_PER_BIT; c++) {
                                        int pidx = k * DESPREAD_CHIPS_PER_BIT + c;
                                        if (pidx >= DESPREAD_PRN_LEN) break;
                                        int base = cs_i + c * isps + epl_off[ei];
                                        float chip_re = 0, chip_im = 0;
                                        for (int s = 0; s < isps; s++) {
                                            int idx = base + s;
                                            if (idx >= 0 && idx < (int)N) {
                                                chip_re += __real__ work[idx];
                                                chip_im += __imag__ work[idx];
                                            }
                                        }
                                        float pi = 2.0f * (float)prn_i[pidx] - 1.0f;
                                        acc_re += chip_re * pi;
                                        acc_im += chip_im * pi;
                                    }
                                    epl[ei] = sqrtf(acc_re * acc_re
                                                    + acc_im * acc_im);
                                }
                                float pwr = 0.0f;
                                for (int c = 0; c < DESPREAD_CHIPS_PER_BIT; c++) {
                                    int base = cs_i + c * isps;
                                    for (int s = 0; s < isps; s++) {
                                        int idx = base + s;
                                        if (idx >= 0 && idx < (int)N) {
                                            float re = __real__ work[idx];
                                            float im = __imag__ work[idx];
                                            pwr += re * re + im * im;
                                        }
                                    }
                                }
                                pwr /= (float)(DESPREAD_CHIPS_PER_BIT * isps);
                                fprintf(epl_csv,
                                        "%d,%d,%d,%.1f,%.1f,%.1f,%.4e\n",
                                        epl_burst, k, cs_i,
                                        (double)epl[0], (double)epl[1],
                                        (double)epl[2], (double)pwr);
                            }
                            fflush(epl_csv);
                        }
                        free(prn_i);
                        free(prn_q);
                    }
                }
            }
        }
        {
            char hex[64];
            for (int i = 0; i < 250; i += 4) {
                int nib = 0;
                for (int b = 0; b < 4 && (i+b) < 250; b++)
                    nib |= (output_bits[i+b] & 1) << (3 - b);
                hex[i/4] = "0123456789ABCDEF"[nib];
            }
            hex[63] = '\0';
            DIAG("[dsss_demod] bits250=%s\n", hex);
        }
    }

cleanup:
    free(work);
    free(chips);
    return rc;
}
