#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dec406.h"
#include "fgb_iq_demod.h"

#define DEFAULT_SAMP_RATE 2457600
#define PRE_SILENCE_MS 100

static int load_cf32(const char *path, float complex **out_iq, size_t *out_n) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -1; }
    fseek(fp, 0, SEEK_END);
    long bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size_t n = bytes / (2 * sizeof(float));
    float complex *iq = malloc(n * sizeof(float complex));
    if (!iq) { fclose(fp); return -1; }
    for (size_t i = 0; i < n; i++) {
        float pair[2];
        if (fread(pair, sizeof(float), 2, fp) != 2) { n = i; break; }
        iq[i] = pair[0] + pair[1] * I;
    }
    fclose(fp);
    *out_iq = iq; *out_n = n;
    return 0;
}

static int load_int16(const char *path, float complex **out_iq, size_t *out_n) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return -1; }
    fseek(fp, 0, SEEK_END);
    long bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size_t n = bytes / (2 * sizeof(int16_t));
    float complex *iq = malloc(n * sizeof(float complex));
    if (!iq) { fclose(fp); return -1; }
    for (size_t i = 0; i < n; i++) {
        int16_t pair[2];
        if (fread(pair, sizeof(int16_t), 2, fp) != 2) { n = i; break; }
        iq[i] = (float)pair[0] / 32768.0f + (float)pair[1] / 32768.0f * I;
    }
    fclose(fp);
    *out_iq = iq; *out_n = n;
    return 0;
}

static void downconvert(float complex *iq, size_t n, double offset_hz, int samp_rate) {
    double w = -2.0 * M_PI * offset_hz / (double)samp_rate;
    for (size_t i = 0; i < n; i++) {
        double ph = w * (double)i;
        float c = (float)cos(ph), s = (float)sin(ph);
        float ri = crealf(iq[i]), im = cimagf(iq[i]);
        iq[i] = (ri * c - im * s) + (ri * s + im * c) * I;
    }
}

/* Find bursts: regions where smoothed |IQ|² exceeds noise floor × factor. */
static int scan_bursts(const float complex *iq, size_t n, int samp_rate,
                       long out_starts[], int max_bursts) {
    long bit_period = samp_rate / 400;
    long win = bit_period * 5;
    long n_win = n / win;
    float *energy = malloc((size_t)n_win * sizeof(float));
    if (!energy) return 0;
    for (long k = 0; k < n_win; k++) {
        float e = 0.0f;
        for (long i = 0; i < win; i++) {
            float r = crealf(iq[k*win + i]), m = cimagf(iq[k*win + i]);
            e += r*r + m*m;
        }
        energy[k] = e;
    }
    /* Sort for median (noise floor estimate) */
    float *sorted = malloc((size_t)n_win * sizeof(float));
    memcpy(sorted, energy, (size_t)n_win * sizeof(float));
    for (long i = 1; i < n_win; i++) {
        float key = sorted[i]; long j = i - 1;
        while (j >= 0 && sorted[j] > key) { sorted[j+1] = sorted[j]; j--; }
        sorted[j+1] = key;
    }
    float noise = sorted[n_win / 2];
    float thresh = noise * 5.0f;
    free(sorted);

    int n_bursts = 0;
    int in_burst = 0;
    long burst_start_k = 0;
    for (long k = 0; k < n_win; k++) {
        if (!in_burst && energy[k] > thresh) {
            in_burst = 1; burst_start_k = k;
        } else if (in_burst && energy[k] < thresh) {
            long burst_len_win = k - burst_start_k;
            if (burst_len_win > 50 && n_bursts < max_bursts) {
                /* Anchor where CW preamble would begin = burst_start - some pre-roll */
                out_starts[n_bursts++] = burst_start_k * win;
            }
            in_burst = 0;
        }
    }
    free(energy);
    return n_bursts;
}

#define TARGET_SPS 24   /* samples per bit after decimation (400 baud -> 9600 Hz) */

/* Single-stage MA + decimation: apply boxcar of length filt_len, keep every M-th. */
static float complex *decim_stage(const float complex *in, size_t n_in, int filt_len,
                                   int M, size_t *n_out) {
    if (M < 1) M = 1;
    if (filt_len < 1) filt_len = 1;
    *n_out = (n_in - filt_len) / M + 1;
    if (*n_out < 1) { *n_out = 0; return NULL; }
    float complex *out = malloc(*n_out * sizeof(float complex));
    if (!out) return NULL;
    /* Running sum for moving average */
    double sum_i = 0.0, sum_q = 0.0;
    for (int j = 0; j < filt_len; j++) {
        sum_i += crealf(in[j]); sum_q += cimagf(in[j]);
    }
    for (size_t k = 0; k < *n_out; k++) {
        size_t idx = (size_t)k * M;
        if (k > 0) {
            size_t old = idx - M;
            for (int j = 0; j < M; j++) {
                sum_i -= crealf(in[old + j]);
                sum_q -= cimagf(in[old + j]);
                size_t ni = idx + filt_len - M + j;
                if (ni < n_in) {
                    sum_i += crealf(in[ni]);
                    sum_q += cimagf(in[ni]);
                }
            }
        }
        float scale = 1.0f / (float)filt_len;
        out[k] = (float)(sum_i * scale) + (float)(sum_q * scale) * I;
    }
    return out;
}

/* Multi-stage filtered decimation. Each stage: MA(filt_len) -> decimate by M.
 * Stages are chosen so that total decimation ~ samp_rate / (TARGET_SPS * 400). */
static float complex *multistage_decimate(const float complex *in, size_t n_in,
                                           int samp_rate, int *out_rate,
                                           size_t *out_n) {
    int target_hz = TARGET_SPS * 400;
    int total_decim = samp_rate / target_hz;
    if (total_decim < 1) total_decim = 1;

    int M1, M2, M3, N1, N2, N3;
    if (total_decim <= 8) {
        N1 = total_decim * 2; M1 = total_decim; N2 = M2 = N3 = M3 = 0;
    } else if (total_decim <= 80) {
        M2 = 4; while (M2 * M2 < total_decim && M2 < 16) M2++;
        M1 = total_decim / M2; if (M1 < 1) M1 = 1;
        N1 = M1 * 2; N2 = M2 * 2; N3 = M3 = 0;
    } else {
        M3 = 2;
        M2 = 8; while (M2 * M3 < total_decim / 8 && M2 < 16) M2++;
        M1 = total_decim / (M2 * M3); if (M1 < 1) M1 = 1;
        N1 = M1 * 2; N2 = M2 * 2; N3 = M3 * 2;
    }

    fprintf(stderr, "Multi-stage decim: total x%d, stage1: MA(%d)/%d, "
            "stage2: MA(%d)/%d", total_decim, N1, M1, N2, M2);
    if (M3) fprintf(stderr, ", stage3: MA(%d)/%d", N3, M3);
    fprintf(stderr, "\n");

    int rate = samp_rate;
    size_t n_src = n_in;
    float complex *s1 = NULL, *s2 = NULL, *s3 = NULL;

    s1 = decim_stage(in, n_src, N1, M1, &n_src);
    if (!s1) goto fail;
    rate /= M1;
    if (M2) {
        s2 = decim_stage(s1, n_src, N2, M2, &n_src);
        free(s1); s1 = NULL;
        if (!s2) goto fail;
        rate /= M2;
    }
    if (M3) {
        s3 = decim_stage(s2 ? s2 : s1, n_src, N3, M3, &n_src);
        if (s2) { free(s2); s2 = NULL; }
        if (!s3) goto fail;
        rate /= M3;
    }

    float complex *result = malloc(n_src * sizeof(float complex));
    if (!result) goto fail;
    if (s3) memcpy(result, s3, n_src * sizeof(float complex));
    else if (s2) memcpy(result, s2, n_src * sizeof(float complex));
    else memcpy(result, s1, n_src * sizeof(float complex));
    free(s1); if (s2) free(s2); if (s3) free(s3);
    *out_rate = rate;
    *out_n = n_src;
    return result;

fail:
    if (s1) free(s1);
    if (s2) free(s2);
    if (s3) free(s3);
    return NULL;
}

int main(int argc, char *argv[]) {
    const char *path = NULL;
    double inject_offset_hz = 0.0;
    double downconvert_hz = 0.0;
    int samp_rate = DEFAULT_SAMP_RATE;
    long burst_start = -1;
    int int16_input = 0;
    int auto_scan = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            inject_offset_hz = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            burst_start = atol(argv[++i]);
        } else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            samp_rate = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
            downconvert_hz = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-i")) {
            int16_input = 1;
        } else if (!strcmp(argv[i], "-A")) {
            auto_scan = 1;
        } else if (argv[i][0] != '-') {
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr,
            "Usage: %s <file> [-r samp_rate] [-i (int16 IQ)] [-f downconvert_hz] [-o inject_hz] [-s burst_start_sample] [-A auto-scan bursts]\n",
            argv[0]);
        return 1;
    }
    if (burst_start < 0) burst_start = (samp_rate / 1000) * PRE_SILENCE_MS;

    float complex *iq = NULL;
    size_t n = 0;
    int rc = int16_input ? load_int16(path, &iq, &n) : load_cf32(path, &iq, &n);
    if (rc != 0 || n == 0) { fprintf(stderr, "load failed\n"); return 1; }

    fprintf(stderr, "Loaded %zu samples (%.3f s at %d Sps, %s)\n",
            n, (double)n / samp_rate, samp_rate, int16_input ? "int16" : "cf32");

    if (downconvert_hz != 0.0) {
        downconvert(iq, n, downconvert_hz, samp_rate);
        fprintf(stderr, "Downconverted by %+.0f Hz\n", downconvert_hz);
    }
    if (inject_offset_hz != 0.0) {
        downconvert(iq, n, -inject_offset_hz, samp_rate);
        fprintf(stderr, "Injected offset %+.1f Hz\n", inject_offset_hz);
    }

    if (samp_rate > 100000) {
        size_t n_dec = 0;
        int dec_rate = 0;
        float complex *iq_dec = multistage_decimate(iq, n, samp_rate, &dec_rate, &n_dec);
        if (!iq_dec) { fprintf(stderr, "decim fail\n"); free(iq); return 1; }
        long decim_ratio = samp_rate / dec_rate;
        if (decim_ratio < 1) decim_ratio = 1;
        fprintf(stderr, "Decimated x%ld -> %d Hz (%zu samples)\n",
                decim_ratio, dec_rate, n_dec);
        free(iq);
        iq = iq_dec; n = n_dec; samp_rate = dec_rate;
        burst_start /= decim_ratio;

        if (getenv("FGB_IQ_DIAG")) {
            long probes[3] = { burst_start / 2,
                               burst_start + samp_rate / 100,
                               burst_start + samp_rate / 5 };
            for (int p = 0; p < 3; p++) {
                long i0 = probes[p], i1 = i0 + samp_rate / 200;
                if (i1 > (long)n) continue;
                double s = 0.0;
                for (long i = i0; i < i1; i++) {
                    float r = crealf(iq[i]), m = cimagf(iq[i]);
                    s += sqrt(r*r + m*m);
                }
                s /= (i1 - i0);
                fprintf(stderr, "[diag] post-decim |IQ| @ %.1f ms = %.4f\n",
                        (double)i0 * 1000.0 / samp_rate, s);
            }
        }
    }

    if (auto_scan) {
        long starts[100];
        int nb = scan_bursts(iq, n, samp_rate, starts, 100);
        fprintf(stderr, "Found %d bursts\n", nb);
        int decoded = 0;
        for (int b = 0; b < nb; b++) {
            uint8_t bits[FGB_LONG_BITS];
            int r = fgb_iq_decode(iq, n, samp_rate, starts[b], bits);
            fprintf(stderr, "Burst %d (start=%ld, %.1f ms): rc=%d\n",
                    b + 1, starts[b], (double)starts[b] * 1000.0 / samp_rate, r);
            if (r == 0) { decode_1g(bits, FGB_LONG_BITS); decoded++; }
        }
        fprintf(stderr, "=== %d/%d decoded ===\n", decoded, nb);
        free(iq);
        return decoded > 0 ? 0 : 2;
    }

    fprintf(stderr, "Decoding at burst_start=%ld (%.1f ms)\n",
            burst_start, (double)burst_start * 1000.0 / samp_rate);
    uint8_t bits[FGB_LONG_BITS];
    int r = fgb_iq_decode(iq, n, samp_rate, burst_start, bits);
    free(iq);
    if (r == 0) { decode_1g(bits, FGB_LONG_BITS); return 0; }
    if (r == -2) { fprintf(stderr, "CRC FAIL\n"); return 2; }
    fprintf(stderr, "Decode failed (rc=%d)\n", r);
    return 1;
}
