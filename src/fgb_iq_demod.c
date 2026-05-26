#include "fgb_iq_demod.h"

#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOL_RATE_HZ   400
#define CW_DURATION_MS   160
#define FSYNC_LEN        9
#define FSYNC_THRESHOLD  7
#define PREAMBLE_BITS    15

extern int test_crc1(const char *s);
extern int test_crc2(const char *s);

static const uint8_t FSYNC_NORMAL[FSYNC_LEN]   = {0,0,0,1,0,1,1,1,1};
static const uint8_t FSYNC_SELFTEST[FSYNC_LEN] = {0,1,1,0,1,0,0,0,0};

static int sync_pattern_ok(const uint8_t *bits) {
    int d_n = 0, d_s = 0;
    for (int i = 0; i < FSYNC_LEN; i++) {
        if (bits[15 + i] != FSYNC_NORMAL[i])   d_n++;
        if (bits[15 + i] != FSYNC_SELFTEST[i]) d_s++;
    }
    return (d_n <= 1 || d_s <= 1);
}

static int crc_ok(const uint8_t *bits, int length) {
    char s[200];
    for (int i = 0; i < length; i++) s[i] = bits[i] ? '1' : '0';
    s[length] = '\0';
    if (!sync_pattern_ok(bits)) return 0;
    if (test_crc1(s)) return 0;
    if (length == 144 && test_crc2(s)) return 0;
    return 1;
}

/* Manchester integrate-and-dump on complex IQ.
 * S1 = sum of first half, S2 = sum of second half.
 * For biphase-L T.001 (±1.1 rad): during data, |S1-S2| ≈ 2A·sin(1.1) ≈ large.
 * During CW (constant phase): |S1-S2| ≈ 0. */
static float complex manchester_symbol(const float complex *iq, long start,
                                        int half) {
    float complex s1 = 0, s2 = 0;
    for (int j = 0; j < half; j++) {
        s1 += iq[start + j];
        s2 += iq[start + half + j];
    }
    return s1 - s2;
}

/* Estimate residual carrier frequency from CW phase slope (sample-level).
 * Nyquist sample rate / 2 — no aliasing for offsets up to fs/2. */
static double estimate_cw_freq(const float complex *iq, long cw_start,
                                int half, double bit_prd, long cw_len,
                                int samp_rate, int diag) {
    (void)half; (void)bit_prd;
    if (cw_len < 2) return 0.0;
    double sum = 0.0;
    long count = 0;
    for (long i = 1; i < cw_len; i++) {
        float complex cross = iq[cw_start + i] * conjf(iq[cw_start + i - 1]);
        sum += cargf(cross);
        count++;
    }
    if (count == 0) return 0.0;
    double dphi_avg = sum / count;
    double freq_hz = dphi_avg / (2.0 * M_PI) * samp_rate;
    if (diag)
        fprintf(stderr, "[fgb_iq] CW freq est: %.1f Hz (n=%ld)\n", freq_hz, count);
    return freq_hz;
}

/* Scan for CW→data transition: compute |S1-S2| for each candidate bit position.
 * CW: constant phase → |S1-S2| ≈ 0.  Data: biphase-L ±1.1 rad → |S1-S2| ≈ 2A·half·sin(1.1).
 * Threshold = 0.25 × expected data level (estimated from CW amplitude). */
static long find_cw_end_cmplx(const float complex *iq, long len,
                               int half, double bit_prd,
                               long search_start, long search_end, int diag) {
    long n_bits = (long)((search_end - search_start) / bit_prd);
    if (n_bits < 20) return -1;

    /* Estimate signal amplitude from CW (first few bits, energy ≈ 0) */
    float cw_mag = 0.0f;
    long cw_count = 0;
    for (long b = 0; b < n_bits / 2; b++) {
        long pos = search_start + (long)(b * bit_prd + 0.5);
        if (pos + half > len) break;
        float s1 = 0.0f;
        for (int j = 0; j < half; j++) s1 += cabsf(iq[pos + j]);
        cw_mag += s1 / half;
        cw_count++;
    }
    if (cw_count == 0) return -1;
    cw_mag /= (float)cw_count;
    /* Expected |S1-S2| for data: 2 × amplitude × half × sin(1.1) ≈ 1.78 × amp × half */
    float expected = 2.0f * cw_mag * (float)half * sinf(1.1f);
    float thresh = expected * 0.3f;
    if (thresh < 0.3f) thresh = 0.3f;

    int sustain = 3;
    int count = 0;
    long edge = -1;
    float prev_e = 0.0f;
    for (long b = 0; b < n_bits; b++) {
        long pos = search_start + (long)(b * bit_prd + 0.5);
        if (pos + (int)(bit_prd + 0.5) > len) break;
        float e = cabsf(manchester_symbol(iq, pos, half));
        /* Average with previous bit to suppress half-bit oscillation */
        float e_smooth = (b > 0) ? 0.5f * (e + prev_e) : e;
        prev_e = e;
        if (e_smooth > thresh) {
            if (count == 0) edge = b;
            count++;
            if (count >= sustain) {
                long result = search_start + (long)(edge * bit_prd + 0.5);
                if (diag)
                    fprintf(stderr, "[fgb_iq] CW end at sample %ld "
                            "(amp=%.3f expect=%.1f thresh=%.1f)\n",
                            result, cw_mag, expected, thresh);
                return result;
            }
        } else {
            count = 0;
        }
    }
    if (diag) fprintf(stderr, "[fgb_iq] CW end not found (amp=%.3f expect=%.1f thresh=%.1f)\n",
                      cw_mag, expected, thresh);
    return -1;
}

/* Refine bit-clock phase: try offsets around data_start, maximize
 * |S1-S2| over the preamble (all ones = guaranteed transitions). */
static long refine_bit_phase_cmplx(const float complex *iq, long data_start,
                                    double bit_prd, int half, long len) {
    long need = (long)((PREAMBLE_BITS + 1) * bit_prd + 0.5);
    if (data_start + need >= len) return data_start;

    float best_score = -1.0f;
    long  best_off   = 0;
    int   step       = half / 4;
    if (step < 1) step = 1;

    for (int off = -half; off <= half; off += step) {
        long cand = data_start + off;
        if (cand < 0 || cand + need >= len) continue;

        float score = 0.0f;
        for (int b = 0; b < PREAMBLE_BITS; b++) {
            long pos = cand + (long)(b * bit_prd + 0.5);
            score += cabsf(manchester_symbol(iq, pos, half));
        }
        if (score > best_score) { best_score = score; best_off = off; }
    }

    long coarse = data_start + best_off;
    for (int off2 = -step; off2 <= step; off2++) {
        long cand = coarse + off2;
        if (cand < 0 || cand + need >= len) continue;

        float score = 0.0f;
        for (int b = 0; b < PREAMBLE_BITS; b++) {
            long pos = cand + (long)(b * bit_prd + 0.5);
            score += cabsf(manchester_symbol(iq, pos, half));
        }
        if (score > best_score) { best_score = score; best_off = off2 + (coarse - data_start); }
    }

    return data_start + best_off;
}

/* Costas loop for BPSK — tracks residual carrier phase on complex soft symbols.
 * Returns the phase-corrected real value for bit decision.
 * Normalized error = I×Q / |sym|² = sin(2θ)/2, independent of signal level. */
static float costas_step(float complex sym, float *phase, float *integrator,
                          float alpha, float beta) {
    float complex rotated = sym * cexpf(-I * *phase);
    float norm2 = crealf(rotated) * crealf(rotated)
                + cimagf(rotated) * cimagf(rotated);
    float error = (norm2 > 1e-9f)
                  ? crealf(rotated) * cimagf(rotated) / norm2
                  : 0.0f;
    *integrator += beta * error;
    /* Clamp integrator: ±0.05 rad/symbol ≈ ±20 rad/s ≈ ±3 Hz at 400 baud */
    if (*integrator >  0.05f) *integrator =  0.05f;
    if (*integrator < -0.05f) *integrator = -0.05f;
    *phase += alpha * error + *integrator;
    while (*phase >  M_PI) *phase -= 2.0f * M_PI;
    while (*phase < -M_PI) *phase += 2.0f * M_PI;
    return crealf(rotated);
}

static void dump_bits(int id, long anchor, int state, const uint8_t *b) {
    static FILE *csv = NULL;
    if (!getenv("FGB_IQ_DIAG")) return;
    if (!csv) {
        csv = fopen("fgb_iq_bits.csv", "w");
        if (csv) fprintf(csv, "burst,anchor,crc_state,bits\n");
    }
    if (!csv) return;
    fprintf(csv, "%d,%ld,%d,", id, anchor, state);
    for (int i = 0; i < FGB_LONG_BITS; i++) fputc(b[i] ? '1' : '0', csv);
    fputc('\n', csv);
    fflush(csv);
}

/* Multi-stage filtered decimation for high-rate input.
 * Uses simple 3-stage MA+decimate (same as decode_fgb_iq.c). */
static int decimate_iq(float complex **iq_ptr, size_t *n_ptr, int *samp_rate,
                        long *burst_start) {
    int sr = *samp_rate;
    if (sr <= 24000) return 0;
    int target_hz = 9600;
    int total_decim = sr / target_hz;
    if (total_decim < 1) total_decim = 1;

    int M1, M2, M3, N1, N2, N3;
    M3 = 2; M2 = 8; M1 = total_decim / (M2 * M3);
    if (M1 < 1) { M1 = total_decim / M2; M3 = 0; }
    if (M1 < 1) { M1 = total_decim; M2 = M3 = 0; }
    N1 = M1 * 2; N2 = M2 * 2; N3 = M3 * 2;

    const float complex *in = *iq_ptr;
    size_t n_in = *n_ptr;

    /* Stage 1 */
    size_t n1 = (n_in - N1) / M1 + 1;
    float complex *s1 = malloc(n1 * sizeof(float complex));
    if (!s1) return -1;
    double sum_i = 0, sum_q = 0;
    for (int j = 0; j < N1; j++) { sum_i += crealf(in[j]); sum_q += cimagf(in[j]); }
    for (size_t k = 0; k < n1; k++) {
        if (k > 0) {
            size_t old = (size_t)(k - 1) * M1;
            for (int j = 0; j < M1; j++) {
                sum_i -= crealf(in[old + j]); sum_q -= cimagf(in[old + j]);
                size_t ni = (size_t)k * M1 + N1 - M1 + j;
                if (ni < n_in) { sum_i += crealf(in[ni]); sum_q += cimagf(in[ni]); }
            }
        }
        float sc = 1.0f / (float)N1;
        s1[k] = (float)(sum_i * sc) + (float)(sum_q * sc) * I;
    }
    sr /= M1;
    /* Stage 2 */
    float complex *s2 = NULL;
    if (M2 && n1 > (size_t)N2) {
        size_t n2 = (n1 - N2) / M2 + 1;
        s2 = malloc(n2 * sizeof(float complex));
        if (!s2) { free(s1); return -1; }
        sum_i = sum_q = 0;
        for (int j = 0; j < N2; j++) { sum_i += crealf(s1[j]); sum_q += cimagf(s1[j]); }
        for (size_t k = 0; k < n2; k++) {
            if (k > 0) {
                size_t old = (size_t)(k - 1) * M2;
                for (int j = 0; j < M2; j++) {
                    sum_i -= crealf(s1[old + j]); sum_q -= cimagf(s1[old + j]);
                    size_t ni = (size_t)k * M2 + N2 - M2 + j;
                    if (ni < n1) { sum_i += crealf(s1[ni]); sum_q += cimagf(s1[ni]); }
                }
            }
            float sc = 1.0f / (float)N2;
            s2[k] = (float)(sum_i * sc) + (float)(sum_q * sc) * I;
        }
        free(s1); s1 = s2; n1 = n2;
        sr /= M2;
    }
    /* Stage 3 */
    if (M3 && n1 > (size_t)N3) {
        size_t n3 = (n1 - N3) / M3 + 1;
        s2 = malloc(n3 * sizeof(float complex));
        if (!s2) { free(s1); return -1; }
        sum_i = sum_q = 0;
        for (int j = 0; j < N3; j++) { sum_i += crealf(s1[j]); sum_q += cimagf(s1[j]); }
        for (size_t k = 0; k < n3; k++) {
            if (k > 0) {
                size_t old = (size_t)(k - 1) * M3;
                for (int j = 0; j < M3; j++) {
                    sum_i -= crealf(s1[old + j]); sum_q -= cimagf(s1[old + j]);
                    size_t ni = (size_t)k * M3 + N3 - M3 + j;
                    if (ni < n1) { sum_i += crealf(s1[ni]); sum_q += cimagf(s1[ni]); }
                }
            }
            float sc = 1.0f / (float)N3;
            s2[k] = (float)(sum_i * sc) + (float)(sum_q * sc) * I;
        }
        free(s1); s1 = s2; n1 = n3;
        sr /= M3;
    }

    *iq_ptr = s1;
    *n_ptr = n1;
    *samp_rate = sr;
    *burst_start /= total_decim;
    return 0;
}

int fgb_iq_decode(const float complex *iq, size_t n, int samp_rate,
                  long burst_start, uint8_t out_bits[FGB_LONG_BITS]) {
    int diag = (getenv("FGB_IQ_DIAG") != NULL);
    static int burst_id = 0;
    if (diag) burst_id++;

    if (!iq || !out_bits || burst_start < 0) return -1;

    /* Internal decimation if sample rate > 24 kHz */
    float complex *iq_dec = NULL;
    int owned_iq = 0;
    if (samp_rate > 24000) {
        if (decimate_iq(&iq_dec, &n, &samp_rate, &burst_start) != 0)
            return -1;
        iq = iq_dec;
        owned_iq = 1;
        if (diag)
            fprintf(stderr, "[fgb_iq] internal decim -> %d Hz (%zu samples)\n",
                    samp_rate, n);
    }

    double bit_prd  = (double)samp_rate / SYMBOL_RATE_HZ;
    int    half_bit = (int)(bit_prd / 2.0 + 0.5);
    long   cw_samp  = (long)CW_DURATION_MS * samp_rate / 1000;
    long   margin   = (long)(50.0 * samp_rate / 1000);

    long w0 = burst_start - margin;
    if (w0 < 1) w0 = 1;
    long w1 = burst_start + cw_samp + (long)(FGB_LONG_BITS * bit_prd) + margin;
    if (w1 >= (long)n) w1 = (long)n - 1;
    long wlen = w1 - w0;
    long need = (long)(FGB_LONG_BITS * bit_prd) + half_bit * 2;
    if (wlen < need) {
        if (diag) fprintf(stderr, "[fgb_iq] burst=%d FAIL buffer short\n", burst_id);
        return -1;
    }

    /* Create working copy (we may apply freq correction) */
    float complex *wiq = malloc((size_t)wlen * sizeof(float complex));
    if (!wiq) { if (owned_iq) free(iq_dec); return -1; }
    memcpy(wiq, iq + w0, (size_t)wlen * sizeof(float complex));

    /* Estimate residual carrier freq from CW zone and wipe it off */
    long fq_cw_start = (burst_start - w0) + cw_samp / 8;
    long fq_cw_len   = cw_samp / 2;
    if (fq_cw_start + fq_cw_len > wlen) fq_cw_len = wlen - fq_cw_start;
    double fq_off = estimate_cw_freq(wiq, fq_cw_start, half_bit, bit_prd,
                                      fq_cw_len, samp_rate, diag);
    if (fabs(fq_off) > 1.0) {
        for (long i = 0; i < wlen; i++)
            wiq[i] *= cexpf(-I * (float)(2.0 * M_PI * fq_off * i / samp_rate));
    }

    /* Step 1: Find CW→data transition in complex domain */
    long cw_search_start = (burst_start - w0) + cw_samp / 4;
    long cw_search_end   = (burst_start - w0) + cw_samp + (long)(30 * bit_prd);
    if (cw_search_end > wlen) cw_search_end = wlen;
    long cw_end = find_cw_end_cmplx(wiq, wlen, half_bit, bit_prd,
                                     cw_search_start, cw_search_end, diag);
    if (cw_end < 0) {
        if (diag) fprintf(stderr, "[fgb_iq] burst=%d CW end not found\n", burst_id);
        return -1;
    }

    /* Step 2: Refine bit-clock phase on preamble */
    long bit0_base = refine_bit_phase_cmplx(wiq, cw_end, bit_prd, half_bit, wlen);
    if (bit0_base + need >= wlen) {
        if (diag) fprintf(stderr, "[fgb_iq] burst=%d bit0 too late\n", burst_id);
        return -1;
    }

    /* Try multiple bit0 offsets around cw_end (±3 half-bits), pick best FSYNC score */
    long best_bit0 = bit0_base;
    int  best_fs_score = -1;
    int  best_flip = 0;
    float best_soft[FGB_LONG_BITS];

    for (int try_off = -3 * half_bit; try_off <= 3 * half_bit; try_off += half_bit / 2) {
        long try_bit0 = cw_end + try_off;
        if (try_bit0 < 0 || try_bit0 + (long)(FGB_LONG_BITS * bit_prd) + half_bit >= wlen)
            continue;

        float phase = 0.785398f, integrator = 0.0f;
        float costas_alpha = 0.10f, costas_beta = 0.0005f;
        uint8_t try_bits[FGB_LONG_BITS];
        for (int b = 0; b < FGB_LONG_BITS; b++) {
            long pos = try_bit0 + (long)(b * bit_prd + 0.5);
            float complex sym = manchester_symbol(wiq, pos, half_bit);
            float s = costas_step(sym, &phase, &integrator, costas_alpha, costas_beta);
            try_bits[b] = (s > 0.0f) ? 1 : 0;
        }

        /* Score frame sync (try both polarities) */
        int mn = 0, ms = 0, mni = 0, msi = 0;
        for (int i = 0; i < FSYNC_LEN; i++) {
            if (try_bits[15+i] == FSYNC_NORMAL[i])        mn++;
            if (try_bits[15+i] == FSYNC_SELFTEST[i])      ms++;
            if ((try_bits[15+i]^1) == FSYNC_NORMAL[i])    mni++;
            if ((try_bits[15+i]^1) == FSYNC_SELFTEST[i])  msi++;
        }
        int score = mn, flip = 0;
        if (ms > score) score = ms;
        if (mni > score) { score = mni; flip = 1; }
        if (msi > score) { score = msi; flip = 1; }

        if (diag)
            fprintf(stderr, "[fgb_iq] off=%+3d bit0=%ld fs=%d/%d\n",
                    try_off, try_bit0 + w0, score, FSYNC_LEN);

        if (score > best_fs_score) {
            best_fs_score = score;
            best_bit0 = try_bit0;
            best_flip = flip;
            /* Re-decode with final bit0 for soft values */
            phase = 0.785398f; integrator = 0.0f;
            for (int b = 0; b < FGB_LONG_BITS; b++) {
                long pos = best_bit0 + (long)(b * bit_prd + 0.5);
                float complex sym = manchester_symbol(wiq, pos, half_bit);
                best_soft[b] = costas_step(sym, &phase, &integrator, costas_alpha, costas_beta);
            }
        }
    }

    long bit0 = best_bit0;
    int need_flip = best_flip;

    if (best_fs_score < FSYNC_THRESHOLD) {
        if (diag) fprintf(stderr, "[fgb_iq] burst=%d FSYNC FAIL best=%d/%d\n",
                          burst_id, best_fs_score, FSYNC_LEN);
        free(wiq);
        return -2;
    }

    for (int b = 0; b < FGB_LONG_BITS; b++) {
        out_bits[b] = (best_soft[b] > 0.0f) ? 1 : 0;
    }
    if (need_flip)
        for (int i = 0; i < FGB_LONG_BITS; i++) out_bits[i] ^= 1;

    /* Step 5: Validate preamble and CRC */
    int pream_ones = 0;
    for (int i = 0; i < PREAMBLE_BITS; i++) pream_ones += out_bits[i];
    if (diag)
        fprintf(stderr, "[fgb_iq] burst=%d preamble=%d/%d fsync=%d/%d %s\n",
                burst_id, pream_ones, PREAMBLE_BITS, best_fs_score, FSYNC_LEN,
                need_flip ? "(flipped)" : "");

    int final_rc = -2;
    if (crc_ok(out_bits, FGB_LONG_BITS) || crc_ok(out_bits, FGB_SHORT_BITS)) {
        if (diag) {
            fprintf(stderr, "[fgb_iq] burst=%d CRC OK\n", burst_id);
            dump_bits(burst_id, bit0 + w0, 1, out_bits);
        }
        final_rc = 0;
    } else {
        /* Try opposite polarity as fallback */
        for (int i = 0; i < FGB_LONG_BITS; i++) out_bits[i] ^= 1;
        if (crc_ok(out_bits, FGB_LONG_BITS) || crc_ok(out_bits, FGB_SHORT_BITS)) {
            if (diag) {
                fprintf(stderr, "[fgb_iq] burst=%d CRC OK (polarity fallback)\n",
                        burst_id);
                dump_bits(burst_id, bit0 + w0, 2, out_bits);
            }
            final_rc = 0;
        } else {
            for (int i = 0; i < FGB_LONG_BITS; i++) out_bits[i] ^= 1;
            if (diag) {
                fprintf(stderr, "[fgb_iq] burst=%d CRC FAIL\n", burst_id);
                dump_bits(burst_id, bit0 + w0, 0, out_bits);
            }
        }
    }
    free(wiq);
    if (owned_iq) free(iq_dec);
    return final_rc;
}
