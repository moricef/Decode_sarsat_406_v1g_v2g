/**
 * @file despread.c
 * @brief T.018 SGB DSSS despreader (PRN gen + 2-pass sync + soft despread)
 *
 * Soft-correlation version: correlates complex chip samples directly with
 * the expected ±1±j pattern instead of hard-slicing to 0/1 first.  This
 * preserves amplitude information critical for weak-signal reception.
 *
 * Input: chip-rate complex samples (output of the Costas loop).
 * Output: 250 message bits interleaved I[0],Q[0],...,I[124],Q[124].
 */

#include "despread.h"
#include "diag_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void despread_gen_prn(uint32_t seed, int length, int8_t *out)
{
    uint32_t state = seed;
    for (int i = 0; i < length; i++) {
        out[i] = (int8_t)(state & 1u);
        uint32_t fb = (state ^ (state >> 18)) & 1u;
        state = (state >> 1) | (fb << 22);
    }
}

void despread_get_preamble_chips(int *out, int n)
{
    int8_t *raw = (int8_t *)malloc((size_t)n);
    if (!raw) return;
    despread_gen_prn(DESPREAD_PRN_SEED_I, n, raw);
    for (int i = 0; i < n; i++)
        out[i] = 1 - 2 * (int)raw[i];  /* 0→+1, 1→-1 */
    free(raw);
}

static void chip_not(const int8_t *src, int n, int8_t *dst)
{
    for (int i = 0; i < n; i++) dst[i] = (int8_t)(1 - src[i]);
}

/* Build expected chip signs (±1.0) for a given Costas phase.
 * pred_i / pred_q are 0/1 tables; output is +1.0 or -1.0. */
static void build_expected(const int8_t *pred_i, const int8_t *pred_q,
                           int n, float *exp_i, float *exp_q)
{
    for (int k = 0; k < n; k++) {
        exp_i[k] = 2.0f * (float)pred_i[k] - 1.0f;
        exp_q[k] = 2.0f * (float)pred_q[k] - 1.0f;
    }
}

int despread_sync(const float complex *samples, int num_chips,
                  despread_sync_t *sync)
{
    if (samples == NULL || sync == NULL ||
        num_chips < DESPREAD_PREAMBLE_CHIPS + DESPREAD_SYNC_RANGE)
        return -1;

    memset(sync, 0, sizeof(*sync));

    int8_t *prn_i = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    int8_t *prn_q = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    int8_t *npi   = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    int8_t *npq   = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    if (!prn_i || !prn_q || !npi || !npq) {
        free(prn_i); free(prn_q); free(npi); free(npq);
        return -1;
    }
    despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PREAMBLE_CHIPS, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, DESPREAD_PREAMBLE_CHIPS, prn_q);
    chip_not(prn_i, DESPREAD_PREAMBLE_CHIPS, npi);
    chip_not(prn_q, DESPREAD_PREAMBLE_CHIPS, npq);

    const int8_t *pred_i_tab[4] = { npi, prn_q, prn_i, npq };
    const int8_t *pred_q_tab[4] = { npq, npi,   prn_q, prn_i };

    /* Pre-compute expected complex signs for each Costas phase. */
    float *exp_i[4], *exp_q[4];
    int alloc_ok = 1;
    for (int p = 0; p < 4; p++) {
        exp_i[p] = (float *)malloc((size_t)DESPREAD_PREAMBLE_CHIPS * sizeof(float));
        exp_q[p] = (float *)malloc((size_t)DESPREAD_PREAMBLE_CHIPS * sizeof(float));
        if (!exp_i[p] || !exp_q[p]) alloc_ok = 0;
    }
    if (!alloc_ok) {
        for (int p = 0; p < 4; p++) { free(exp_i[p]); free(exp_q[p]); }
        free(prn_i); free(prn_q); free(npi); free(npq);
        return -1;
    }
    for (int p = 0; p < 4; p++)
        build_expected(pred_i_tab[p], pred_q_tab[p],
                       DESPREAD_PREAMBLE_CHIPS, exp_i[p], exp_q[p]);

    /* ---------- Pass A: complex correlation to find offset + phase ----------
     * |Σ s·conj(e)| is insensitive to carrier phase — 3x z-score vs real-only. */
    int search_hi = DESPREAD_SYNC_RANGE;
    int msg_limit = num_chips - DESPREAD_TOTAL_BITS * DESPREAD_CHIPS_PER_BIT;
    if (msg_limit < search_hi) search_hi = msg_limit;
    if (search_hi < 1) search_hi = 1;
    if (DESPREAD_PREAMBLE_CHIPS + search_hi > num_chips)
        search_hi = num_chips - DESPREAD_PREAMBLE_CHIPS;

    float best_i_abs = -1e30f, best_i_raw = 0.0f;
    int   best_off_i = 0, best_phase = 0;
    float second_i_abs = -1e30f, second_i_raw = 0.0f;
    int   second_off_i = 0;
    float sum_i = 0.0f, sum2_i = 0.0f;
    int   cnt_i = 0;

    for (int off = 0; off < search_hi; off++) {
        for (int p = 0; p < 4; p++) {
            float corr_re = 0.0f, corr_im = 0.0f;
            for (int k = 0; k < DESPREAD_PREAMBLE_CHIPS; k++) {
                float re = __real__ samples[off + k];
                float im = __imag__ samples[off + k];
                corr_re += re * exp_i[p][k] + im * exp_q[p][k];
                corr_im += im * exp_i[p][k] - re * exp_q[p][k];
            }
            float corr = sqrtf(corr_re * corr_re + corr_im * corr_im);
            sum_i += corr;
            sum2_i += corr * corr;
            cnt_i++;
            if (corr > best_i_abs) {
                if (abs(off - best_off_i) >= DESPREAD_CHIPS_PER_BIT) {
                    second_i_abs = best_i_abs;
                    second_i_raw = best_i_raw;
                    second_off_i = best_off_i;
                }
                best_i_abs = corr;
                best_i_raw = corr;
                best_off_i = off;
                best_phase = p;
            } else if (corr > second_i_abs &&
                       abs(off - best_off_i) >= DESPREAD_CHIPS_PER_BIT) {
                second_i_abs = corr;
                second_i_raw = corr;
                second_off_i = off;
            }
        }
    }
    /* Resolve 4-phase ambiguity: all phases give same magnitude.
     * Pick the phase with the largest (most positive) real part. */
    {
        float best_re = -1e30f;
        int   best_p = best_phase;
        for (int p = 0; p < 4; p++) {
            float re_sum = 0.0f;
            for (int k = 0; k < DESPREAD_PREAMBLE_CHIPS; k++) {
                float re = __real__ samples[best_off_i + k];
                float im = __imag__ samples[best_off_i + k];
                re_sum += re * exp_i[p][k] + im * exp_q[p][k];
            }
            if (re_sum > best_re) { best_re = re_sum; best_p = p; }
        }
        best_phase = best_p;
    }

    /* ---------- Pass B: complex correlation around best I offset ---------- */
    int qlo = best_off_i - 5; if (qlo < 0) qlo = 0;
    int qhi = best_off_i + 6; if (qhi > search_hi) qhi = search_hi;

    float best_q_abs = -1e30f, best_q_raw = 0.0f;
    int   best_off_q = best_off_i;
    float sum_q = 0.0f, sum2_q = 0.0f;
    int   cnt_q = 0;

    for (int off = qlo; off < qhi; off++) {
        float corr_re = 0.0f, corr_im = 0.0f;
        for (int k = 0; k < DESPREAD_PREAMBLE_CHIPS; k++) {
            float re = __real__ samples[off + k];
            float im = __imag__ samples[off + k];
            corr_re += re * exp_i[best_phase][k] + im * exp_q[best_phase][k];
            corr_im += im * exp_i[best_phase][k] - re * exp_q[best_phase][k];
        }
        float corr = sqrtf(corr_re * corr_re + corr_im * corr_im);
        sum_q += corr;
        sum2_q += corr * corr;
        cnt_q++;
        if (corr > best_q_abs) {
            best_q_abs = corr;
            best_q_raw = corr;
            best_off_q = off;
        }
    }

    /* Quality: theoretical noise std for complex correlation magnitude */
    float noise_std = sqrtf((float)DESPREAD_PREAMBLE_CHIPS * 0.429f);
    float z_i, z_q;
    if (cnt_i > 50) {
        float mean_i = (sum_i - best_i_raw) / (float)(cnt_i - 1);
        float var_i = (sum2_i - best_i_raw * best_i_raw) / (float)(cnt_i - 1)
                      - mean_i * mean_i;
        if (var_i < 1e-10f) var_i = 1e-10f;
        z_i = (best_i_raw - mean_i) / sqrtf(var_i);
    } else {
        z_i = best_i_raw / noise_std;
    }
    z_q = best_q_raw / noise_std;

    float z_comb = sqrtf(z_i * z_i + z_q * z_q);
    float thr = DESPREAD_SYNC_THRESHOLD;

    float z2_fail = 0.0f;
    if (cnt_i > 50 && second_i_raw > 0.0f) {
        float mean_i_f = (sum_i - best_i_raw) / (float)(cnt_i - 1);
        float var_i_f = (sum2_i - best_i_raw * best_i_raw) / (float)(cnt_i - 1)
                      - mean_i_f * mean_i_f;
        if (var_i_f < 1e-10f) var_i_f = 1e-10f;
        z2_fail = (second_i_raw - mean_i_f) / sqrtf(var_i_f);
    }

    if (z_comb < thr) {
        /* Expose z even on failure so callers sweeping a parameter (e.g.
         * wipeoff frequency) can pick the best sub-threshold candidate. */
        sync->z_comb = z_comb;
        sync->z1 = z_i;
        float ratio_f = (z2_fail > 0.1f) ? z_i / z2_fail : 999.0f;
        DIAG("[despread] SYNC FAILED: "
             "I z=%.1f Q z=%.1f combined=%.1f (need %.1f) "
             "peak=%.3e 2nd=%.3e z1/z2=%.1f/%.1f=%.2f "
             "lag1=%d lag2=%d mean=%.3e std=%.3e\n",
             (double)z_i, (double)z_q, (double)z_comb, (double)thr,
             (double)best_i_abs, (double)second_i_abs,
             (double)z_i, (double)z2_fail, (double)ratio_f,
             best_off_i, second_off_i,
             cnt_i > 1 ? (double)(sum_i / (float)cnt_i) : 0.0,
             cnt_i > 50 ? (double)sqrtf((sum2_i/(float)cnt_i)
                          - (sum_i/(float)cnt_i)*(sum_i/(float)cnt_i))
                        : 0.0);
        for (int p = 0; p < 4; p++) { free(exp_i[p]); free(exp_q[p]); }
        free(prn_i); free(prn_q); free(npi); free(npq);
        return -1;
    }

    sync->off_i   = best_off_i;
    sync->off_q   = best_off_q;
    sync->phase   = best_phase;
    sync->z_comb  = z_comb;
    sync->score_i = (z_i < 10000.0f) ? (int)(z_i * 10.0f) : 0x7FFF;
    sync->score_q = (z_i < 10000.0f) ? (int)(z_q * 10.0f) : 0x7FFF;
    sync->z1      = z_i;
    sync->z2      = z2_fail;
    sync->lag1    = best_off_i;
    sync->lag2    = second_off_i;

    float ratio = (z2_fail > 0.1f) ? z_i / z2_fail : 999.0f;
    DIAG("[despread] Synced: off_I=%d (z=%.1f), off_Q=%d (z=%.1f), "
         "combined=%.1f, phase=%d°, z1/z2=%.1f/%.1f=%.2f lag2=%d\n",
         best_off_i, (double)z_i,
         best_off_q, (double)z_q,
         (double)z_comb, best_phase * 90,
         (double)z_i, (double)z2_fail, (double)ratio,
         second_off_i);

    for (int p = 0; p < 4; p++) { free(exp_i[p]); free(exp_q[p]); }
    free(prn_i); free(prn_q); free(npi); free(npq);
    return 0;
}

int despread_bits(const float complex *samples, int num_chips,
                  const despread_sync_t *sync,
                  const despread_pll_cfg_t *pll_cfg,
                  despread_metrics_t *metrics,
                  uint8_t *output_bits)
{
    if (samples == NULL || sync == NULL || output_bits == NULL)
        return -1;

    int off_i = sync->off_i;
    int off_q = sync->off_q;
    int phase = sync->phase;

    /* Full-length PRN for message despreading (raw, NOT predicted). */
    int8_t *prn_i = (int8_t *)malloc(DESPREAD_PRN_LEN);
    int8_t *prn_q = (int8_t *)malloc(DESPREAD_PRN_LEN);
    if (!prn_i || !prn_q) { free(prn_i); free(prn_q); return -1; }
    despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PRN_LEN, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, DESPREAD_PRN_LEN, prn_q);

    /* Phase tracking: 2nd-order (proportional + integral).
     * alpha corrects phase; beta accumulates the per-bit phase drift
     * (freq_per_bit) to cancel a constant frequency residual instead of
     * letting it produce a phase ramp. A 1st-order tracker (alpha only)
     * lags the residual and flips bits mid-message
     * (measured drift ~0.29 Hz -> 0.012 rad/bit). */
    float phase_rad = (float)phase * (float)M_PI / 2.0f;
    float freq_per_bit = pll_cfg ? pll_cfg->freq_init : 0.0f;
    const float alpha = 0.04f;
    const float beta  = (pll_cfg && pll_cfg->freq_locked) ? 0.0f : 0.01f;

    float ri_re_pre_sum = 0.0f, ri_re_msg_sum = 0.0f;

    /* Diagnostic dump (DSSS_DIAG): one CSV row per bit per burst.
     * Goal: compare phase_rad / freq_per_bit / re·im trajectories of
     * BCH-OK vs BCH-FAIL bursts of equivalent SNR. Enable with:
     *   DSSS_DIAG=1 ./build/dec406_scan ...
     * Output: despread_bits.csv */
    static FILE *diag_csv = NULL;
    static FILE *timing_csv = NULL;
    static int   diag_burst_id = 0;
    int diag_on = (getenv("DSSS_DIAG") != NULL);
    if (diag_on) {
        if (!diag_csv) {
            diag_csv = fopen("despread_bits.csv", "w");
            if (diag_csv)
                fprintf(diag_csv,
                        "burst,bit,phase_rad,freq_per_bit,"
                        "ri_re,ri_im,rq_re,rq_im,d_i,d_q,e\n");
        }
        if (!timing_csv) {
            timing_csv = fopen("chip_timing.csv", "w");
            if (timing_csv)
                fprintf(timing_csv,
                        "burst,bit,cm2,cm1,c0,cp1,cp2\n");
        }
        diag_burst_id++;
        DIAG("[diag] despread burst=%d\n", diag_burst_id);
    }

    float pre_phi[DESPREAD_PREAMBLE_BITS];

    int out_idx = 0;
    for (int k = 0; k < DESPREAD_TOTAL_BITS; k++) {
        int cs_i = off_i + k * DESPREAD_CHIPS_PER_BIT;
        int cs_q = off_q + k * DESPREAD_CHIPS_PER_BIT;
        if (cs_i + DESPREAD_CHIPS_PER_BIT > num_chips ||
            cs_q + DESPREAD_CHIPS_PER_BIT > num_chips)
            break;

        float ci_re = 0.0f, ci_im = 0.0f;
        float cq_re = 0.0f, cq_im = 0.0f;
        for (int c = 0; c < DESPREAD_CHIPS_PER_BIT; c++) {
            float ei = 2.0f * (float)prn_i[k * DESPREAD_CHIPS_PER_BIT + c] - 1.0f;
            float eq = 2.0f * (float)prn_q[k * DESPREAD_CHIPS_PER_BIT + c] - 1.0f;
            float si_re = __real__ samples[cs_i + c];
            float si_im = __imag__ samples[cs_i + c];
            ci_re += si_re * ei;  ci_im += si_im * ei;
            float sq_re = __real__ samples[cs_q + c];
            float sq_im = __imag__ samples[cs_q + c];
            cq_re += sq_re * eq;  cq_im += sq_im * eq;
        }

        if (k < DESPREAD_PREAMBLE_BITS)
            pre_phi[k] = atan2f(ci_im, ci_re);

        float cp = cosf(phase_rad), sp = sinf(phase_rad);
        float ri_re = ci_re * cp + ci_im * sp;
        float ri_im = ci_im * cp - ci_re * sp;
        float rq_re = cq_re * cp + cq_im * sp;
        float rq_im = cq_im * cp - cq_re * sp;

        uint8_t d_i = (ri_re < 0.0f) ? 1 : 0;
        uint8_t d_q = (rq_im < 0.0f) ? 1 : 0;

        if (k < DESPREAD_PREAMBLE_BITS)
            ri_re_pre_sum += fabsf(ri_re);
        else
            ri_re_msg_sum += fabsf(ri_re);

        if (k >= DESPREAD_PREAMBLE_BITS && out_idx + 2 <= DESPREAD_OUTPUT_BITS) {
            output_bits[out_idx]     = d_i;
            output_bits[out_idx + 1] = d_q;
            out_idx += 2;
        }

        /* Phase update: BPSK Costas, I real→Re*Im, Q imag→-Re*Im.
         * 2nd-order: alpha on phase, beta on freq_per_bit (integral). */
        float perr = ri_re * ri_im - rq_re * rq_im;
        float pow = ri_re*ri_re + ri_im*ri_im + rq_re*rq_re + rq_im*rq_im;
        float e_val = 0.0f;
        if (pow > 1e-10f) {
            e_val = perr / pow;
            if (fabsf(e_val) > 0.01f) {   /* dead zone — suppress noise */
                phase_rad    += alpha * e_val;
                freq_per_bit += beta  * e_val;
            }
        }
        if (diag_on && diag_csv) {
            fprintf(diag_csv,
                    "%d,%d,%.6f,%.6f,%.4e,%.4e,%.4e,%.4e,%u,%u,%.6f\n",
                    diag_burst_id, k,
                    (double)phase_rad, (double)freq_per_bit,
                    (double)ri_re, (double)ri_im,
                    (double)rq_re, (double)rq_im,
                    (unsigned)d_i, (unsigned)d_q, (double)e_val);
        }
        if (diag_on && timing_csv) {
            float corr[5];
            for (int s = -2; s <= 2; s++) {
                float acc = 0.0f;
                for (int c = 0; c < DESPREAD_CHIPS_PER_BIT; c++) {
                    int idx = cs_i + c + s;
                    int pidx = k * DESPREAD_CHIPS_PER_BIT + c;
                    if (idx >= 0 && idx < num_chips &&
                        pidx >= 0 && pidx < DESPREAD_PRN_LEN) {
                        float ei = 2.0f * (float)prn_i[pidx] - 1.0f;
                        float re = __real__ samples[idx];
                        float im = __imag__ samples[idx];
                        acc += re * ei * cp + im * ei * sp;
                    }
                }
                corr[s + 2] = fabsf(acc);
            }
            fprintf(timing_csv,
                    "%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                    diag_burst_id, k,
                    (double)corr[0], (double)corr[1],
                    (double)corr[2], (double)corr[3],
                    (double)corr[4]);
        }
        phase_rad += freq_per_bit;        /* apply learned drift each bit */

        if (k == DESPREAD_PREAMBLE_BITS - 1) {
            for (int i = 1; i < DESPREAD_PREAMBLE_BITS; i++) {
                float d = pre_phi[i] - pre_phi[i - 1];
                if (d > (float)M_PI)  pre_phi[i] -= 2.0f * (float)M_PI;
                if (d < -(float)M_PI) pre_phi[i] += 2.0f * (float)M_PI;
            }
            float sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (int i = 0; i < DESPREAD_PREAMBLE_BITS; i++) {
                float fi = (float)i;
                sx += fi; sy += pre_phi[i];
                sxx += fi * fi; sxy += fi * pre_phi[i];
            }
            float fn = (float)DESPREAD_PREAMBLE_BITS;
            float det = fn * sxx - sx * sx;
            if (fabsf(det) > 1e-6f) {
                float slope = (fn * sxy - sx * sy) / det;
                float intercept = (sy - slope * sx) / fn;
                freq_per_bit = slope;
                phase_rad = intercept + slope * (fn - 1.0f);
                DIAG("[despread] preamble fit: intercept=%.3f slope=%.4f "
                     "rad/bit (%.2f Hz) phase_rad=%.3f\n",
                     (double)intercept, (double)slope,
                     (double)(slope * 38400.0 / (256.0 * 2.0 * M_PI)),
                     (double)(phase_rad + slope));
            }
        }
    }
    if (diag_on && diag_csv) fflush(diag_csv);
    if (diag_on && timing_csv) fflush(timing_csv);

    if (metrics) {
        metrics->ri_re_pre_avg = ri_re_pre_sum / (float)DESPREAD_PREAMBLE_BITS;
        metrics->ri_re_msg_avg = ri_re_msg_sum / (float)DESPREAD_MSG_BITS;
    }

    free(prn_i); free(prn_q);
    return (out_idx == DESPREAD_OUTPUT_BITS) ? 0 : -1;
}

int despread_burst(const float complex *samples, int num_chips,
                   uint8_t *output_bits, float *z_score)
{
    despread_sync_t sync;
    if (despread_sync(samples, num_chips, &sync) != 0)
        return -1;
    if (z_score) {
        *z_score = sync.z_comb;
    }
    return despread_bits(samples, num_chips, &sync, NULL, NULL, output_bits);
}
