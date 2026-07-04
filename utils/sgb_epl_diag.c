/**
 * @file sgb_epl_diag.c
 * @brief Offline SGB preamble EPL/Prompt diagnostics for cf32 burst windows.
 *
 * This tool is intentionally outside the real-time decoder path.  It mirrors
 * the current flat-chain front-end up to chip-rate samples, then reports
 * block-level prompt coherence and rough early/late metrics over the SGB
 * preamble.  Its purpose is to answer one question before any tracking work:
 * does a failed burst contain stable PRN prompt energy that a loop could use?
 */

#include "despread.h"
#include "freq_acq.h"

#include <complex.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int lag;
    int phase;
    float z;
    float peak;
    float second;
    float mean;
    float std;
} preamble_search_t;

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-s sample_rate] [-c chip_rate] file.cf32\n"
            "  default sample_rate: 2457600\n"
            "  default chip_rate  : 38400\n",
            argv0);
}

static float complex *read_cf32(const char *path, size_t *n_out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 0 || (sz % (long)sizeof(float complex)) != 0) {
        fprintf(stderr, "%s: not a cf32 file size\n", path);
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    size_t n = (size_t)sz / sizeof(float complex);
    float complex *buf = (float complex *)malloc(n * sizeof(float complex));
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, sizeof(float complex), n, fp) != n) {
        fprintf(stderr, "read %s failed\n", path);
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *n_out = n;
    return buf;
}

static void dc_block(float complex *x, size_t n)
{
    float dc_i = 0.0f, dc_q = 0.0f;
    const float alpha = 0.001f;
    for (size_t t = 0; t < n; t++) {
        float r = crealf(x[t]);
        float q = cimagf(x[t]);
        dc_i += alpha * (r - dc_i);
        dc_q += alpha * (q - dc_q);
        x[t] = (r - dc_i) + (q - dc_q) * I;
    }
}

static void nco_wipe(float complex *x, size_t n, float freq_hz, float fs)
{
    float dphi = -2.0f * (float)M_PI * freq_hz / fs;
    float complex step = cosf(dphi) + sinf(dphi) * I;
    float complex ph = 1.0f + 0.0f * I;
    for (size_t k = 0; k < n; k++) {
        x[k] *= ph;
        ph *= step;
        if ((k & 1023u) == 0u) {
            float a = cabsf(ph);
            if (a > 0.0f) ph /= a;
        }
    }
}

static void oqpsk_delay(float complex *x, size_t n, int delay)
{
    for (size_t t = 0; t < n; t++) {
        float r = crealf(x[t]);
        float q = (t + (size_t)delay < n) ? cimagf(x[t + (size_t)delay]) : 0.0f;
        x[t] = r + q * I;
    }
}

static void boxcar_decimate_off(const float complex *in, size_t n,
                                float sps, int offset,
                                float complex *out, size_t n_chips)
{
    int isps = (int)(sps + 0.5f);
    for (size_t k = 0; k < n_chips; k++) {
        float complex acc = 0.0f;
        size_t base = (size_t)((double)k * sps + (double)offset + 0.5);
        for (int j = 0; j < isps; j++) {
            size_t idx = base + (size_t)j;
            if (idx < n) acc += in[idx];
        }
        out[k] = acc;
    }
}

static void build_expected(const int8_t *chip_i, const int8_t *chip_q,
                           int n, float *ei, float *eq)
{
    for (int k = 0; k < n; k++) {
        ei[k] = chip_i[k] ? 1.0f : -1.0f;
        eq[k] = chip_q[k] ? 1.0f : -1.0f;
    }
}

static void chip_not(const int8_t *in, int n, int8_t *out)
{
    for (int i = 0; i < n; i++) out[i] = (int8_t)!in[i];
}

static int make_expected(float **ei_out, float **eq_out)
{
    int n = DESPREAD_PREAMBLE_CHIPS;
    int8_t *prn_i = (int8_t *)malloc((size_t)n);
    int8_t *prn_q = (int8_t *)malloc((size_t)n);
    int8_t *npi = (int8_t *)malloc((size_t)n);
    int8_t *npq = (int8_t *)malloc((size_t)n);
    if (!prn_i || !prn_q || !npi || !npq) {
        free(prn_i); free(prn_q); free(npi); free(npq);
        return -1;
    }
    despread_gen_prn(DESPREAD_PRN_SEED_I, n, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, n, prn_q);
    chip_not(prn_i, n, npi);
    chip_not(prn_q, n, npq);

    const int8_t *pred_i[4] = { npi, prn_q, prn_i, npq };
    const int8_t *pred_q[4] = { npq, npi, prn_q, prn_i };
    for (int p = 0; p < 4; p++) {
        ei_out[p] = (float *)malloc((size_t)n * sizeof(float));
        eq_out[p] = (float *)malloc((size_t)n * sizeof(float));
        if (!ei_out[p] || !eq_out[p]) {
            for (int j = 0; j <= p; j++) {
                free(ei_out[j]);
                free(eq_out[j]);
            }
            free(prn_i); free(prn_q); free(npi); free(npq);
            return -1;
        }
        build_expected(pred_i[p], pred_q[p], n, ei_out[p], eq_out[p]);
    }

    free(prn_i); free(prn_q); free(npi); free(npq);
    return 0;
}

static float corr_mag(const float complex *chips, int off,
                      const float *ei, const float *eq, int n,
                      float *phase_out)
{
    float re_sum = 0.0f, im_sum = 0.0f;
    for (int k = 0; k < n; k++) {
        float re = crealf(chips[off + k]);
        float im = cimagf(chips[off + k]);
        re_sum += re * ei[k] + im * eq[k];
        im_sum += im * ei[k] - re * eq[k];
    }
    if (phase_out) *phase_out = atan2f(im_sum, re_sum);
    return sqrtf(re_sum * re_sum + im_sum * im_sum);
}

static int preamble_search(const float complex *chips, int n_chips,
                           float **ei, float **eq,
                           preamble_search_t *out)
{
    if (!chips || !out || n_chips < DESPREAD_PREAMBLE_CHIPS + 1)
        return -1;

    int search_hi = DESPREAD_SYNC_RANGE;
    int msg_limit = n_chips - DESPREAD_TOTAL_BITS * DESPREAD_CHIPS_PER_BIT;
    if (msg_limit < search_hi) search_hi = msg_limit;
    if (DESPREAD_PREAMBLE_CHIPS + search_hi > n_chips)
        search_hi = n_chips - DESPREAD_PREAMBLE_CHIPS;
    if (search_hi < 1) return -1;

    float best = -1.0f, second = -1.0f;
    int best_lag = 0, best_phase = 0, second_lag = 0;
    double sum = 0.0, sum2 = 0.0;
    int cnt = 0;

    for (int off = 0; off < search_hi; off++) {
        for (int p = 0; p < 4; p++) {
            float c = corr_mag(chips, off, ei[p], eq[p],
                               DESPREAD_PREAMBLE_CHIPS, NULL);
            sum += c;
            sum2 += (double)c * c;
            cnt++;
            if (c > best) {
                if (abs(off - best_lag) >= DESPREAD_CHIPS_PER_BIT) {
                    second = best;
                    second_lag = best_lag;
                }
                best = c;
                best_lag = off;
                best_phase = p;
            } else if (c > second &&
                       abs(off - best_lag) >= DESPREAD_CHIPS_PER_BIT) {
                second = c;
                second_lag = off;
            }
        }
    }

    /* Magnitude is phase-ambiguous; select the Costas phase with max real sum. */
    float best_re = -1e30f;
    for (int p = 0; p < 4; p++) {
        float re_sum = 0.0f;
        for (int k = 0; k < DESPREAD_PREAMBLE_CHIPS; k++) {
            float re = crealf(chips[best_lag + k]);
            float im = cimagf(chips[best_lag + k]);
            re_sum += re * ei[p][k] + im * eq[p][k];
        }
        if (re_sum > best_re) {
            best_re = re_sum;
            best_phase = p;
        }
    }

    double mean = (cnt > 1) ? (sum - best) / (double)(cnt - 1) : 0.0;
    double var = (cnt > 1) ? (sum2 - (double)best * best) / (double)(cnt - 1)
                            - mean * mean : 0.0;
    if (var < 1e-12) var = 1e-12;

    out->lag = best_lag;
    out->phase = best_phase;
    out->z = (float)(((double)best - mean) / sqrt(var));
    out->peak = best;
    out->second = second;
    out->mean = (float)mean;
    out->std = (float)sqrt(var);
    (void)second_lag;
    return 0;
}

static void block_corr(const float complex *chips, int n_chips,
                       int start, const float *ei, const float *eq,
                       int n, float *mag, float *coh, float *phase)
{
    if (start < 0 || start + n > n_chips) {
        *mag = *coh = *phase = 0.0f;
        return;
    }

    float re_sum = 0.0f, im_sum = 0.0f;
    float e_sig = 0.0f;
    for (int k = 0; k < n; k++) {
        float re = crealf(chips[start + k]);
        float im = cimagf(chips[start + k]);
        re_sum += re * ei[k] + im * eq[k];
        im_sum += im * ei[k] - re * eq[k];
        e_sig += re * re + im * im;
    }

    *mag = sqrtf(re_sum * re_sum + im_sum * im_sum);
    *coh = (e_sig > 1e-12f) ? *mag / sqrtf(e_sig * (float)(2 * n)) : 0.0f;
    *phase = atan2f(im_sum, re_sum);
}

static float unwrap_delta(float d)
{
    while (d > (float)M_PI) d -= 2.0f * (float)M_PI;
    while (d < -(float)M_PI) d += 2.0f * (float)M_PI;
    return d;
}

int main(int argc, char **argv)
{
    float fs = 2457600.0f;
    float chip_rate = 38400.0f;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            fs = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            chip_rate = (float)atof(argv[++i]);
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 2;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        usage(argv[0]);
        return 2;
    }

    float sps = fs / chip_rate;
    int isps = (int)(sps + 0.5f);
    size_t n = 0;
    float complex *samples = read_cf32(path, &n);
    if (!samples) return 1;
    if (n < (size_t)fs) {
        fprintf(stderr, "warning: short window: %.3f s\n", (double)n / fs);
    }

    dc_block(samples, n);

    size_t n_chips = (size_t)((double)n / sps);
    float complex *chips = (float complex *)calloc(n_chips, sizeof(float complex));
    if (!chips) {
        free(samples);
        return 1;
    }
    boxcar_decimate_off(samples, n, sps, 0, chips, n_chips);

    int n_chips_acq = (n_chips > 12000) ? 12000 : (int)n_chips;
    int acq_max_lag = (int)(0.21f * chip_rate);
    if (acq_max_lag > n_chips_acq - 1024)
        acq_max_lag = n_chips_acq - 1024;

    freq_acq_result_t acq;
    memset(&acq, 0, sizeof(acq));
    int acq_rc = freq_acq_fft_corr(chips, n_chips_acq, chip_rate,
                                   -8000.0f, 8000.0f,
                                   acq_max_lag, &acq);

    float **ei = (float **)calloc(4, sizeof(float *));
    float **eq = (float **)calloc(4, sizeof(float *));
    if (!ei || !eq || make_expected(ei, eq) != 0) {
        fprintf(stderr, "expected PRN allocation failed\n");
        free(ei); free(eq); free(chips); free(samples);
        return 1;
    }

    float best_freq = acq.freq_hz;
    float best_z = -1.0f;
    preamble_search_t best_sync;
    memset(&best_sync, 0, sizeof(best_sync));

    for (int df = -15; df <= 15; df += 5) {
        float complex *tmp = (float complex *)malloc(n * sizeof(float complex));
        float complex *c = (float complex *)calloc(n_chips, sizeof(float complex));
        if (!tmp || !c) {
            free(tmp); free(c);
            continue;
        }
        memcpy(tmp, samples, n * sizeof(float complex));
        nco_wipe(tmp, n, acq.freq_hz + (float)df, fs);
        oqpsk_delay(tmp, n, isps / 2);
        boxcar_decimate_off(tmp, n, sps, 0, c, n_chips);

        preamble_search_t s;
        if (preamble_search(c, (int)n_chips, ei, eq, &s) == 0 && s.z > best_z) {
            best_z = s.z;
            best_freq = acq.freq_hz + (float)df;
            best_sync = s;
        }
        free(tmp);
        free(c);
    }

    nco_wipe(samples, n, best_freq, fs);
    oqpsk_delay(samples, n, isps / 2);
    boxcar_decimate_off(samples, n, sps, 0, chips, n_chips);

    preamble_search_t sync;
    if (preamble_search(chips, (int)n_chips, ei, eq, &sync) != 0) {
        fprintf(stderr, "preamble search failed\n");
        for (int p = 0; p < 4; p++) { free(ei[p]); free(eq[p]); }
        free(ei); free(eq); free(chips); free(samples);
        return 1;
    }

    fprintf(stderr,
            "file=%s samples=%zu fs=%.0f sps=%.3f chips=%zu\n"
            "acq rc=%d freq=%.0fHz conf=%.1f phase=%d\n"
            "refined freq=%.0fHz sync_z=%.1f lag=%d phase=%d peak=%.3e "
            "second=%.3e mean=%.3e std=%.3e\n",
            path, n, (double)fs, (double)sps, n_chips,
            acq_rc, (double)acq.freq_hz, (double)acq.confidence,
            acq.costas_phase,
            (double)best_freq, (double)sync.z, sync.lag, sync.phase,
            (double)sync.peak, (double)sync.second,
            (double)sync.mean, (double)sync.std);

    puts("block,chip_start,prompt_mag,prompt_coh,prompt_phase_rad,"
         "freq_from_prev_hz,early_mag,late_mag,dll_chip");

    float prev_phase = 0.0f;
    int have_prev = 0;
    const float block_t = (float)DESPREAD_CHIPS_PER_BIT / chip_rate;
    for (int b = 0; b < DESPREAD_PREAMBLE_BITS; b++) {
        int rel = b * DESPREAD_CHIPS_PER_BIT;
        int start = sync.lag + rel;
        float pm, pc, pp;
        float em, ec, ep;
        float lm, lc, lp;
        block_corr(chips, (int)n_chips, start,
                   ei[sync.phase] + rel, eq[sync.phase] + rel,
                   DESPREAD_CHIPS_PER_BIT, &pm, &pc, &pp);
        block_corr(chips, (int)n_chips, start - 1,
                   ei[sync.phase] + rel, eq[sync.phase] + rel,
                   DESPREAD_CHIPS_PER_BIT, &em, &ec, &ep);
        block_corr(chips, (int)n_chips, start + 1,
                   ei[sync.phase] + rel, eq[sync.phase] + rel,
                   DESPREAD_CHIPS_PER_BIT, &lm, &lc, &lp);

        float freq = 0.0f;
        if (have_prev)
            freq = unwrap_delta(pp - prev_phase) / (2.0f * (float)M_PI * block_t);
        prev_phase = pp;
        have_prev = 1;

        float dll = 0.0f;
        float den = em * em + lm * lm;
        if (den > 1e-12f)
            dll = 0.5f * (em * em - lm * lm) / den;

        printf("%d,%d,%.6g,%.6f,%.6f,%.6f,%.6g,%.6g,%.6f\n",
               b, start, (double)pm, (double)pc, (double)pp,
               (double)freq, (double)em, (double)lm, (double)dll);
        (void)ec; (void)ep; (void)lc; (void)lp;
    }

    for (int p = 0; p < 4; p++) { free(ei[p]); free(eq[p]); }
    free(ei); free(eq); free(chips); free(samples);
    return 0;
}
