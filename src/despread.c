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
     * Uses complex dot product |Σ s[k] × conj(e[k])| which is insensitive
     * to carrier phase (phase factors out as |exp(jθ)| = 1). */
    int search_hi = DESPREAD_SYNC_RANGE;
    int msg_limit = num_chips - DESPREAD_TOTAL_BITS * DESPREAD_CHIPS_PER_BIT;
    if (msg_limit < search_hi) search_hi = msg_limit;
    if (search_hi < 1) search_hi = 1;
    if (DESPREAD_PREAMBLE_CHIPS + search_hi > num_chips)
        search_hi = num_chips - DESPREAD_PREAMBLE_CHIPS;

    float best_i_abs = -1e30f, best_i_raw = 0.0f;
    int   best_off_i = 0, best_phase = 0;
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
                best_i_abs = corr;
                best_i_raw = (corr_re > 0.0f) ? corr : -corr;
                best_off_i = off;
                best_phase = p;
            }
        }
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
            best_q_raw = (corr_re > 0.0f) ? corr : -corr;
            best_off_q = off;
        }
    }

    /* ---------- Quality check: peak z-score ----------
     * For each pass we measure how many standard deviations the best
     * correlation stands above the mean of the other candidates.
     * Pure noise gives z < 3; a real preamble gives z >> 10.
     *
     * When there are too few candidates for a reliable empirical
     * variance (cnt < 10), fall back to the theoretical noise floor:
     * std = sqrt(N_chips) ≈ 80 for 6400-chip preamble. */
    /* Complex correlation magnitude is Rayleigh-distributed under noise.
     * Use theoretical noise std = sqrt(N * var(|CN(0,2)|)) with var≈0.429. */
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
    /* Pass B has only ~11 candidates — empirical variance unreliable. */
    z_q = best_q_raw / noise_std;

    float z_comb = sqrtf(z_i * z_i + z_q * z_q);
    float thr = DESPREAD_SYNC_THRESHOLD;
    if (z_comb < thr) {
        fprintf(stderr,
                "[despread] SYNC FAILED: "
                "I z=%.1f Q z=%.1f combined=%.1f (need %.1f)\n",
                (double)z_i, (double)z_q, (double)z_comb, (double)thr);
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

    fprintf(stderr,
            "[despread] Synced: off_I=%d (z=%.1f), off_Q=%d (z=%.1f), "
            "combined=%.1f, phase=%d°\n",
            best_off_i, (double)z_i,
            best_off_q, (double)z_q,
            (double)z_comb, best_phase * 90);

    for (int p = 0; p < 4; p++) { free(exp_i[p]); free(exp_q[p]); }
    free(prn_i); free(prn_q); free(npi); free(npq);
    return 0;
}

int despread_bits(const float complex *samples, int num_chips,
                  const despread_sync_t *sync,
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

    int out_idx = 0;
    for (int k = 0; k < DESPREAD_TOTAL_BITS; k++) {
        int cs_i = off_i + k * DESPREAD_CHIPS_PER_BIT;
        int cs_q = off_q + k * DESPREAD_CHIPS_PER_BIT;
        if (cs_i + DESPREAD_CHIPS_PER_BIT > num_chips ||
            cs_q + DESPREAD_CHIPS_PER_BIT > num_chips)
            break;

        /* Soft correlation: sum(re * (2*prn-1)) over 256 chips.
         * The I/Q swap and inversion logic matches the original hard-slicer
         * (see git history), using raw PRN_i / PRN_q directly. */
        float corr_i = 0.0f, corr_q = 0.0f;
        for (int c = 0; c < DESPREAD_CHIPS_PER_BIT; c++) {
            float exp_i = 2.0f * (float)prn_i[k * DESPREAD_CHIPS_PER_BIT + c] - 1.0f;
            float exp_q = 2.0f * (float)prn_q[k * DESPREAD_CHIPS_PER_BIT + c] - 1.0f;

            switch (phase) {
            case 0:
                corr_i += __real__ samples[cs_i + c] * exp_i;
                corr_q += __imag__ samples[cs_q + c] * exp_q;
                break;
            case 1: /* 90°: I↔Q swap */
                corr_i += __imag__ samples[cs_q + c] * exp_i;
                corr_q += __real__ samples[cs_i + c] * exp_q;
                break;
            case 2: /* 180°: no swap */
                corr_i += __real__ samples[cs_i + c] * exp_i;
                corr_q += __imag__ samples[cs_q + c] * exp_q;
                break;
            default: /* 270°: I↔Q swap */
                corr_i += __imag__ samples[cs_q + c] * exp_i;
                corr_q += __real__ samples[cs_i + c] * exp_q;
                break;
            }
        }

        uint8_t d_i, d_q;
        switch (phase) {
        case 0:  /* 0°: I→I, Q→Q */
            d_i = (corr_i > 0.0f) ? 1 : 0;
            d_q = (corr_q > 0.0f) ? 1 : 0;
            break;
        case 1:  /* 90°: I→Q, Q→-I */
            d_i = (corr_i > 0.0f) ? 0 : 1;
            d_q = (corr_q > 0.0f) ? 1 : 0;
            break;
        case 2:  /* 180°: I→-I, Q→-Q */
            d_i = (corr_i > 0.0f) ? 0 : 1;
            d_q = (corr_q > 0.0f) ? 0 : 1;
            break;
        default: /* 270°: I→-Q, Q→I */
            d_i = (corr_i > 0.0f) ? 1 : 0;
            d_q = (corr_q > 0.0f) ? 0 : 1;
            break;
        }

        if (k >= DESPREAD_PREAMBLE_BITS && out_idx + 2 <= DESPREAD_OUTPUT_BITS) {
            output_bits[out_idx]     = d_i;
            output_bits[out_idx + 1] = d_q;
            out_idx += 2;
        }
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
    return despread_bits(samples, num_chips, &sync, output_bits);
}
