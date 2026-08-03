#include "fgb_iq_demod.h"
#include "diag_log.h"
#include "country_codes.h"

#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYMBOL_RATE_HZ   400
#define CW_DURATION_MS   160
#define FSYNC_LEN        9
#define FSYNC_THRESHOLD  6
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

static int frame_crc_ok(const uint8_t *bits) {
    int length = bits[24] ? FGB_LONG_BITS : FGB_SHORT_BITS;
    return crc_ok(bits, length);
}

static int is_orbitography(const uint8_t *bits) {
    return bits[25] && !bits[36] && !bits[37] && !bits[38];
}

/* BCH1 brute-force correction: try flipping 1-3 bits in the BCH1
 * codeword (bits 24..105) and check if syndrome clears.
 * Returns number of corrected bits (0 = no correction needed/found). */
static int bch1_correct(uint8_t *bits, int length) {
    if (length < 106) return 0;
    char s[200];
    for (int i = 0; i < length; i++) s[i] = bits[i] ? '1' : '0';
    s[length] = '\0';
    if (!test_crc1(s)) return 0;

    for (int a = 24; a < 106; a++) {
        s[a] ^= 1;
        if (!test_crc1(s)) { bits[a] ^= 1; return 1; }
        for (int b = a + 1; b < 106; b++) {
            s[b] ^= 1;
            if (!test_crc1(s)) { bits[a] ^= 1; bits[b] ^= 1; return 2; }
            for (int c = b + 1; c < 106; c++) {
                s[c] ^= 1;
                if (!test_crc1(s)) { bits[a] ^= 1; bits[b] ^= 1; bits[c] ^= 1; return 3; }
                s[c] ^= 1;
            }
            s[b] ^= 1;
        }
        s[a] ^= 1;
    }
    return 0;
}

/* BCH2 protects bits 106..143 (zero-based) of long frames and corrects
 * up to two errors. */
static int bch2_correct(uint8_t *bits) {
    char s[FGB_LONG_BITS + 1];
    for (int i = 0; i < FGB_LONG_BITS; i++) s[i] = bits[i] ? '1' : '0';
    s[FGB_LONG_BITS] = '\0';
    if (!test_crc2(s)) return 0;

    for (int a = 106; a < 144; a++) {
        s[a] ^= 1;
        if (!test_crc2(s)) {
            bits[a] ^= 1;
            return 1;
        }
        for (int b = a + 1; b < 144; b++) {
            s[b] ^= 1;
            if (!test_crc2(s)) {
                bits[a] ^= 1;
                bits[b] ^= 1;
                return 2;
            }
            s[b] ^= 1;
        }
        s[a] ^= 1;
    }
    return -1;
}

static int correct_frame(uint8_t *bits, int *length,
                         int *bch1_fixed, int *bch2_fixed) {
    *bch1_fixed = bch1_correct(bits, FGB_LONG_BITS);
    char s[FGB_LONG_BITS + 1];
    for (int i = 0; i < FGB_LONG_BITS; i++) s[i] = bits[i] ? '1' : '0';
    s[FGB_LONG_BITS] = '\0';
    if (test_crc1(s)) return 0;

    *length = bits[24] ? FGB_LONG_BITS : FGB_SHORT_BITS;
    *bch2_fixed = 0;
    if (*length == FGB_LONG_BITS && !is_orbitography(bits)) {
        *bch2_fixed = bch2_correct(bits);
        if (*bch2_fixed < 0) return 0;
    }
    if (!(is_orbitography(bits) ? sync_pattern_ok(bits) : frame_crc_ok(bits)))
        return 0;

    /* Country code sanity check. BCH1 corrects up to 3 errors and CRC1 is only
     * 24 bits wide, so a burst decoded from noise can still produce a
     * structurally valid frame — with a country code that does not exist.
     * Genuine traffic always carries an assigned MID (locally 227/228 for
     * France); every phantom observed on the 1544 downlink and on local runs
     * showed a one-off unassigned code (995, 908, 867, 830, 796, ...).
     * This is the same rejection the official Cospas-Sarsat decoder applies
     * ("Unknown Country Code"). */
    int country = 0;
    for (int i = 0; i < 10; i++) country = (country << 1) | (bits[26 + i] & 1);
    if (strcmp(get_country_name(country), "Unknown") == 0)
        return 0;

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
    DIAG("[fgb_iq] CW freq est: %.1f Hz (n=%ld)\n", freq_hz, count);
    return freq_hz;
}

/* Scan for CW→data transition: compute |S1-S2| for each candidate bit position.
 * CW: constant phase → |S1-S2| ≈ 0.  Data: biphase-L ±1.1 rad → |S1-S2| ≈ 2A·half·sin(1.1).
 * Threshold = 0.25 × expected data level (estimated from CW amplitude). */
static long find_cw_end_cmplx(const float complex *iq, long len,
                               int half, double bit_prd,
                               long search_start, long search_end, int diag,
                               float *out_cw_mag, float *out_expected,
                               float *out_thresh) {
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
    float thresh = expected * 0.2f;
    if (thresh < 0.08f) thresh = 0.08f;
    if (out_cw_mag) *out_cw_mag = cw_mag;
    if (out_expected) *out_expected = expected;
    if (out_thresh) *out_thresh = thresh;

    int sustain = 4;
    long best_result = -1;
    int half_step = half / 2;
    if (half_step < 1) half_step = 1;

    for (int phase = 0; phase < 2; phase++) {
        long offset = phase * half_step;
        int count = 0;
        long edge = -1;
        float prev_e = 0.0f;
        for (long b = 0; b < n_bits; b++) {
            long pos = search_start + offset + (long)(b * bit_prd + 0.5);
            if (pos + (int)(bit_prd + 0.5) > len) break;
            float e = cabsf(manchester_symbol(iq, pos, half));
            float e_smooth = (b > 0) ? 0.5f * (e + prev_e) : e;
            prev_e = e;
            if (e_smooth > thresh) {
                if (count == 0) edge = b;
                count++;
                if (count >= sustain) {
                    long result = search_start + offset + (long)(edge * bit_prd + 0.5);
                    if (best_result < 0 || result < best_result)
                        best_result = result;
                    break;
                }
            } else {
                count = 0;
            }
        }
    }

    if (best_result >= 0) {
        if (diag)
            DIAG("[fgb_iq] CW end at sample %ld "
                    "(amp=%.3f expect=%.1f thresh=%.1f)\n",
                    best_result, cw_mag, expected, thresh);
        return best_result;
    }
    if (diag) DIAG("[fgb_iq] CW end not found (amp=%.3f expect=%.1f thresh=%.1f)\n",
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

static void dump_bits(int id, long anchor, int state, const uint8_t *b,
                      const float *soft) {
    static FILE *csv = NULL;
    if (!getenv("FGB_IQ_DIAG")) return;
    if (!csv) {
        csv = fopen("fgb_iq_bits.csv", "w");
        if (csv) fprintf(csv, "burst,anchor,crc_state,bits,soft\n");
    }
    if (!csv) return;
    fprintf(csv, "%d,%ld,%d,", id, anchor, state);
    for (int i = 0; i < FGB_LONG_BITS; i++) fputc(b[i] ? '1' : '0', csv);
    if (soft) {
        fputc(',', csv);
        for (int i = 0; i < FGB_LONG_BITS; i++)
            fprintf(csv, "%s%.3f", i ? " " : "", soft[i]);
    }
    fputc('\n', csv);
    fflush(csv);
}

static void dump_costas_diag(int id, double fq_off, long bit0, int fs_score,
                             long cw_end_raw, float cw_mag, float cw_expected,
                             float cw_thresh,
                             const float complex *wiq, int half_bit,
                             double bit_prd, long wlen) {
    static FILE *csv = NULL;
    if (!getenv("FGB_IQ_DIAG")) return;
    if (!csv) {
        csv = fopen("fgb_costas_diag.csv", "w");
        if (csv) fprintf(csv, "burst,fq_off,bit0,fs_score,"
                         "cw_end,cw_mag,cw_expected,cw_thresh,"
                         "ph0_pream,ph0_sat,ph0_phase_end,"
                         "ph45_pream,ph45_sat,ph45_phase_end,"
                         "ph90_pream,ph90_sat,ph90_phase_end,"
                         "ph135_pream,ph135_sat,ph135_phase_end\n");
    }
    if (!csv) return;

    float init_phases[4] = {0.0f, 0.785398f, 1.570796f, 2.356194f};
    int   pream[4];
    int   sat_count[4];
    float phase_end[4];

    for (int p = 0; p < 4; p++) {
        float phase = init_phases[p], integrator = 0.0f;
        int sat = 0;
        for (int b = 0; b < FGB_LONG_BITS; b++) {
            long pos = bit0 + (long)(b * bit_prd + 0.5);
            if (pos + half_bit * 2 > wlen) break;
            float complex sym = manchester_symbol(wiq, pos, half_bit);
            float old_int = integrator;
            costas_step(sym, &phase, &integrator, 0.10f, 0.0005f);
            if (fabsf(old_int) >= 0.0499f && fabsf(integrator) >= 0.0499f)
                sat++;
        }
        sat_count[p] = sat;
        phase_end[p] = phase;
        int ones = 0;
        phase = init_phases[p]; integrator = 0.0f;
        for (int b = 0; b < PREAMBLE_BITS; b++) {
            long pos = bit0 + (long)(b * bit_prd + 0.5);
            if (pos + half_bit * 2 > wlen) break;
            float complex sym = manchester_symbol(wiq, pos, half_bit);
            float s = costas_step(sym, &phase, &integrator, 0.10f, 0.0005f);
            if (s > 0.0f) ones++;
        }
        pream[p] = ones;
    }

    fprintf(csv, "%d,%.1f,%ld,%d,%ld,%.4f,%.4f,%.4f", id, fq_off, bit0, fs_score,
            cw_end_raw, cw_mag, cw_expected, cw_thresh);
    for (int p = 0; p < 4; p++)
        fprintf(csv, ",%d,%d,%.3f", pream[p], sat_count[p], phase_end[p]);
    fputc('\n', csv);
    fflush(csv);
}

/* Multi-stage filtered decimation for high-rate input.
 * Uses simple 3-stage MA+decimate (same as decode_fgb_iq.c). */
static int decimate_iq(const float complex *in, size_t n_in,
                        float complex **out_ptr, size_t *n_out,
                        int *samp_rate, long *burst_start) {
    int sr = *samp_rate;
    if (sr <= 24000) { *out_ptr = NULL; return 0; }
    int target_hz = 9600;
    int total_decim = sr / target_hz;
    if (total_decim < 1) total_decim = 1;

    int M1, M2, M3, N1, N2, N3;
    M3 = 2; M2 = 8; M1 = total_decim / (M2 * M3);
    if (M1 < 1) { M1 = total_decim / M2; M3 = 0; }
    if (M1 < 1) { M1 = total_decim; M2 = M3 = 0; }
    N1 = M1 * 2; N2 = M2 * 2; N3 = M3 * 2;

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

    *out_ptr = s1;
    *n_out = n1;
    *samp_rate = sr;
    *burst_start /= (M1 * (M2 ? M2 : 1) * (M3 ? M3 : 1));
    return 0;
}

int fgb_iq_decode(const float complex *iq, size_t n, int samp_rate,
                  long burst_start, uint8_t out_bits[FGB_LONG_BITS],
                  int *out_length) {
    if (!out_length) return -1;
    *out_length = 0;
    int diag = (getenv("FGB_IQ_DIAG") != NULL);
    static int burst_id = 0;
    burst_id++;

    if (!iq || !out_bits || burst_start < 0) return -1;

    /* Internal decimation if sample rate > 24 kHz */
    float complex *iq_dec = NULL;
    size_t n_dec = 0;
    int    owned_iq = 0;
    if (samp_rate > 24000) {
        if (decimate_iq(iq, n, &iq_dec, &n_dec, &samp_rate, &burst_start) != 0) {
            DIAG("[fgb_iq] burst=%d FAIL decimation\n", burst_id);
            return -1;
        }
        iq = iq_dec;
        n = n_dec;
        owned_iq = 1;
        if (diag)
            DIAG("[fgb_iq] internal decim -> %d Hz (%zu samples)\n",
                    samp_rate, n);
    }

    /* Normalize the working buffer to unit RMS so the FGB demod is
     * scale-invariant: RTL 8-bit, Airspy float, any gain or signal level
     * decode identically. Absolute thresholds downstream (e.g. the CW-end
     * floor) then sit at a consistent fraction of the signal. Scaling the
     * whole buffer leaves every relative decision unchanged. */
    {
        double sumsq = 0.0;
        for (size_t i = 0; i < n; i++) {
            float complex s = iq[i];
            sumsq += (double)crealf(s) * crealf(s) + (double)cimagf(s) * cimagf(s);
        }
        double rms = (n && sumsq > 0.0) ? sqrt(sumsq / (double)n) : 0.0;
        if (rms > 0.0) {
            if (!owned_iq) {
                float complex *work = malloc(n * sizeof(float complex));
                if (!work) return -1;
                memcpy(work, iq, n * sizeof(float complex));
                iq = work; iq_dec = work; owned_iq = 1;
            }
            float g = (float)(1.0 / rms);
            for (size_t i = 0; i < n; i++) iq_dec[i] *= g;
        }
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
        DIAG("[fgb_iq] burst=%d FAIL buffer short\n", burst_id);
        if (owned_iq) free(iq_dec);
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
    float cw_mag = 0.0f, cw_expected = 0.0f, cw_thresh = 0.0f;
    long cw_end = find_cw_end_cmplx(wiq, wlen, half_bit, bit_prd,
                                     cw_search_start, cw_search_end, diag,
                                     &cw_mag, &cw_expected, &cw_thresh);
    long cw_min = (burst_start - w0) + cw_samp * 3 / 5;
    /* An early CW end means the carrier-to-data transition was not reliably
     * located: the sync is then placed at cw_min by the retry below, i.e. at a
     * fixed guess, and the bits that follow are noise the BCH can still force
     * into a valid codeword. On strong signals (firmin: 0 of 1449 CRC OK) this
     * never happens; on the weak 1544 downlink every phantom decode shows it.
     * We keep the retry but flag the burst so its output is rejected even if
     * the CRC passes — the preamble count is a poor discriminator (46 genuine
     * firmin decodes pass with a low preamble, 13 of them at 0/15 via the
     * polarity path), whereas this flag separates real from phantom cleanly. */
    int cw_unreliable = 0;
    if (cw_end >= 0 && cw_end < cw_min) {
        cw_unreliable = 1;
        DIAG("[fgb_iq] burst=%d CW end too early %ld < %ld, retrying\n",
                burst_id, cw_end, cw_min);
        cw_end = find_cw_end_cmplx(wiq, wlen, half_bit, bit_prd,
                                     cw_min, cw_search_end, diag,
                                     &cw_mag, &cw_expected, &cw_thresh);
    }
    if (cw_end < 0) {
        DIAG("[fgb_iq] burst=%d CW end not found (amp=%.3f fq=%.1f n=%zu sr=%d bs=%ld)\n",
                burst_id, cabsf(wiq[(burst_start - w0) + cw_samp/2]),
                fq_off, n, samp_rate, burst_start);
        free(wiq); if (owned_iq) free(iq_dec);
        return -1;
    }

    /* Step 2: Refine bit-clock phase on preamble */
    long bit0_base = refine_bit_phase_cmplx(wiq, cw_end, bit_prd, half_bit, wlen);
    if (bit0_base + need >= wlen) {
        DIAG("[fgb_iq] burst=%d bit0 too late\n", burst_id);
        free(wiq); if (owned_iq) free(iq_dec);
        return -1;
    }

    /* Try multiple bit0 offsets around cw_end (±3 half-bits), pick best FSYNC score */
    long best_bit0 = bit0_base;
    int  best_fs_score = -1;
    int  best_flip = 0;
    float best_soft[FGB_LONG_BITS];

    float try_phases[4] = {0.0f, 0.785398f, 1.570796f, 2.356194f};
    float best_init_phase = 0.785398f;

    for (int try_off = -3 * half_bit; try_off <= 3 * half_bit; try_off += half_bit / 2) {
        long try_bit0 = cw_end + try_off;
        if (try_bit0 < 0 || try_bit0 + (long)(FGB_LONG_BITS * bit_prd) + half_bit >= wlen)
            continue;

        for (int ph = 0; ph < 4; ph++) {
            float phase = try_phases[ph], integrator = 0.0f;
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

            if (diag && score > best_fs_score)
                DIAG("[fgb_iq] off=%+3d ph=%d° bit0=%ld fs=%d/%d\n",
                        try_off, ph * 45, try_bit0 + w0, score, FSYNC_LEN);

            if (score > best_fs_score) {
                best_fs_score = score;
                best_bit0 = try_bit0;
                best_flip = flip;
                best_init_phase = try_phases[ph];
                phase = try_phases[ph]; integrator = 0.0f;
                for (int b = 0; b < FGB_LONG_BITS; b++) {
                    long pos = best_bit0 + (long)(b * bit_prd + 0.5);
                    float complex sym = manchester_symbol(wiq, pos, half_bit);
                    best_soft[b] = costas_step(sym, &phase, &integrator, costas_alpha, costas_beta);
                }
            }
        }
    }

    long bit0 = best_bit0;
    int need_flip = best_flip;

    dump_costas_diag(burst_id, fq_off, bit0, best_fs_score,
                     cw_end, cw_mag, cw_expected, cw_thresh,
                     wiq, half_bit, bit_prd, wlen);

    if (best_fs_score < FSYNC_THRESHOLD) {
        DIAG("[fgb_iq] burst=%d FSYNC FAIL best=%d/%d\n",
                burst_id, best_fs_score, FSYNC_LEN);
        free(wiq); if (owned_iq) free(iq_dec);
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
    DIAG("[fgb_iq] burst=%d preamble=%d/%d fsync=%d/%d ph=%.0f° %s\n",
            burst_id, pream_ones, PREAMBLE_BITS, best_fs_score, FSYNC_LEN,
            (double)(best_init_phase * 180.0f / (float)M_PI),
            need_flip ? "(flipped)" : "");

    uint8_t sliced_bits[FGB_LONG_BITS];
    memcpy(sliced_bits, out_bits, sizeof(sliced_bits));

    int frame_length = 0;
    int bch1_fixed = 0;
    int bch2_fixed = 0;
    int final_rc = -2;
    if (correct_frame(out_bits, &frame_length, &bch1_fixed, &bch2_fixed)) {
        DIAG("[fgb_iq] burst=%d CRC OK (BCH1 corrected %d, BCH2 corrected %d)\n",
             burst_id, bch1_fixed, bch2_fixed);
        dump_bits(burst_id, bit0 + w0, 1, out_bits, best_soft);
        *out_length = frame_length;
        final_rc = 0;
    } else {
        memcpy(out_bits, sliced_bits, sizeof(sliced_bits));
        for (int i = 0; i < FGB_LONG_BITS; i++) out_bits[i] ^= 1;
        if (correct_frame(out_bits, &frame_length, &bch1_fixed, &bch2_fixed)) {
            DIAG("[fgb_iq] burst=%d CRC OK (polarity, BCH1 corrected %d, BCH2 corrected %d)\n",
                 burst_id, bch1_fixed, bch2_fixed);
            dump_bits(burst_id, bit0 + w0, 2, out_bits, best_soft);
            *out_length = frame_length;
            final_rc = 0;
        } else {
            memcpy(out_bits, sliced_bits, sizeof(sliced_bits));
            DIAG("[fgb_iq] burst=%d CRC FAIL\n", burst_id);
            dump_bits(burst_id, bit0 + w0, 0, out_bits, best_soft);
        }
    }
    /* Reject a CRC-valid frame whose CW end was unreliable: on the weak 1544
     * downlink this is a phantom the BCH forced into shape. -2 = "no usable
     * frame" (same as CRC FAIL to the caller). */
    if (final_rc == 0 && cw_unreliable) {
        DIAG("[fgb_iq] burst=%d rejected: CRC OK but CW end unreliable\n", burst_id);
        final_rc = -2;
    }
    free(wiq);
    if (owned_iq) free(iq_dec);
    return final_rc;
}
