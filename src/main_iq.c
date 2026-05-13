/**
 * @file main_iq.c
 * @brief COSPAS-SARSAT 2G IQ demodulator. Scans file with sliding window.
 *
 * Usage: ./dec406_iq <file> [-s rate] [-f freq] [-u] [-i]
 *   -u  RTL-SDR uint8 input
 *   -i  int16 interleaved input
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "dsss_demod.h"
#include "dec406.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BURST_BLK 1024

static int cmp_float(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

static size_t find_burst_start(const float complex *buf, size_t n, float fs) {
    int dec = (int)(fs / 100000.0f + 0.5f);
    if (dec < 2) dec = 2;
    size_t nd = n / (size_t)dec;

    size_t nblk = nd / BURST_BLK;
    if (nblk < 8) return 0;

    float *pwr = malloc(nblk * sizeof(float));
    float *sorted = malloc(nblk * sizeof(float));
    if (!pwr || !sorted) { free(pwr); free(sorted); return 0; }

    for (size_t b = 0; b < nblk; b++) {
        float s = 0.0f;
        for (size_t j = 0; j < BURST_BLK; j++) {
            size_t dec_off = (b * BURST_BLK + j) * (size_t)dec;
            float re = 0.0f, im = 0.0f;
            for (int d = 0; d < dec; d++) {
                re += __real__ buf[dec_off + (size_t)d];
                im += __imag__ buf[dec_off + (size_t)d];
            }
            re /= (float)dec; im /= (float)dec;
            s += re * re + im * im;
        }
        pwr[b] = s / (float)BURST_BLK;
    }

    memcpy(sorted, pwr, nblk * sizeof(float));
    qsort(sorted, nblk, sizeof(float), cmp_float);

    float noise = sorted[nblk / 10];
    float sig   = sorted[nblk * 9 / 10];
    size_t result = 0;

    if (noise > 0 && sig / noise > 3.0f) {
        float thr = sqrtf(noise * sig);
        for (size_t b = 0; b < nblk; b++) {
            if (pwr[b] > thr) {
                result = (b > 1 ? b - 1 : 0) * BURST_BLK;
                fprintf(stderr,
                        "[main_iq] burst at blk %zu (%.3f ms), "
                        "trimming %zu samples  P10=%.1e P90=%.1e\n",
                        b, (double)(result) / (double)fs * 1000.0,
                        result, (double)noise, (double)sig);
                break;
            }
        }
    }

    free(pwr); free(sorted);
    return result;
}

static void print_usage(const char *p) {
    printf("Usage: %s <file> [-s rate] [-f freq] [-u] [-i] [-I]\n", p);
    printf("  -s  Sample rate (default 2457600)\n");
    printf("  -f  Frequency offset in Hz\n");
    printf("  -u  RTL-SDR uint8 input\n");
    printf("  -i  int16 input\n");
    printf("  -I  int32 input (SDRangel ci32_le)\n");
}

static void print_hex(const uint8_t *bits, int len) {
    printf("\n=== Demodulated bits (hex, 63 chars) ===\n");
    if (len == 250) {
        for (int i = 0; i < 252; i += 4) {
            uint8_t n = 0;
            for (int j = 0; j < 4; j++) {
                int idx = i + j - 2;
                n = (uint8_t)((n << 1) | ((idx < 0 || idx >= len) ? 0 : bits[idx]));
            }
            printf("%X", n);
        }
        printf("\n");
    }
}

static size_t read_win(FILE *fp, int u8, int i16, int i32, size_t n, float complex *dst) {
    if (u8) {
        unsigned char *r = malloc(n * 2);
        if (!r) return 0;
        size_t k = fread(r, 2, n, fp);
        for (size_t i = 0; i < k; i++)
            dst[i] = ((float)r[2*i]-127.5f)/127.5f + ((float)r[2*i+1]-127.5f)/127.5f * I;
        free(r);
        return k;
    }
    if (i16) {
        short *r = malloc(n * 2 * sizeof(short));
        if (!r) return 0;
        size_t k = fread(r, sizeof(short)*2, n, fp);
        for (size_t i = 0; i < k; i++)
            dst[i] = (float)r[2*i]/32768.0f + (float)r[2*i+1]/32768.0f * I;
        free(r);
        return k;
    }
    if (i32) {
        int *r = malloc(n * 2 * sizeof(int));
        if (!r) return 0;
        size_t k = fread(r, sizeof(int)*2, n, fp);
        for (size_t i = 0; i < k; i++)
            dst[i] = (float)r[2*i]/4194304.0f + (float)r[2*i+1]/4194304.0f * I;
        free(r);
        return k;
    }
    return fread(dst, sizeof(float complex), n, fp);
}

int main(int argc, char *argv[]) {
    const char *fn = NULL;
    float fs = 2457600.0f, foff = 0.0f;
    int u8 = 0, i16 = 0, i32 = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"-s") && i+1<argc) fs = atof(argv[++i]);
        else if (!strcmp(argv[i],"-f") && i+1<argc) foff = atof(argv[++i]);
        else if (!strcmp(argv[i],"-u")) u8 = 1;
        else if (!strcmp(argv[i],"-i")) i16 = 1;
        else if (!strcmp(argv[i],"-I")) i32 = 1;
        else fn = argv[i];
    }
    if (!fn) { print_usage(argv[0]); return 1; }

    float sps = fs / 38400.0f;
    if (fabsf(fs/sps - 38400.0f) > 100.0f) { fprintf(stderr,"Bad rate\n"); return 1; }

    printf("Rate: %.0f Hz  SPS: %.3f\n", (double)fs, (double)sps);

    uint8_t out[DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS];
    memset(out, 0, sizeof(out));

    size_t win = (size_t)(fs * 1.1f);
    float complex *buf = calloc(win, sizeof(float complex));
    if (!buf) return 1;

    FILE *fp = fopen(fn, "rb");
    if (!fp) { fprintf(stderr,"Cannot open %s\n", fn); free(buf); return 1; }

    /* File size */
    fseek(fp, 0, SEEK_END);
    long fbytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    int bps = u8 ? 2 : (i16 ? 4 : 8);
    size_t total = (size_t)fbytes / bps;
    printf("File: %.1f MB, %zu samples (%.1f s)\n", (double)fbytes/1e6, total, (double)total/fs);

    if (total < win) win = total;
    size_t step = (size_t)(fs * 0.25);
    int found = 0;
    float best_z = 0.0f;
    uint8_t *best_out = calloc(DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS, 1);
    if (!best_out) { fclose(fp); free(buf); return 1; }

    for (size_t off = 0; off + win <= total; off += step) {
        fseek(fp, (long)(off * bps), SEEK_SET);
        size_t n = read_win(fp, u8, i16, i32, win, buf);
        if (n < win/2) break;

        printf("\rScan t=%.2fs...", off/fs); fflush(stdout);

        if (fabsf(foff) > 0.5f) {
            for (size_t k = 0; k < n; k++) {
                float ph = -2.0f*(float)M_PI*foff*(float)k/fs;
                float c = cosf(ph), s = sinf(ph);
                float re = __real__ buf[k], im = __imag__ buf[k];
                buf[k] = (re*c - im*s) + (re*s + im*c)*I;
            }
        }

        size_t trim = find_burst_start(buf, n, fs);
        float z = 0.0f;
        if (dsss_receive_burst(buf + trim, n - trim, sps, fs, 0, out, &z) == 0) {
            printf("\r  Sync at t=%.2fs (z=%.1f)\n", (double)(off + trim) / (double)fs, (double)z);
            if (z > best_z) { best_z = z; memcpy(best_out, out, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS); }
            found++;
        }
    }
    fclose(fp);

    if (!found) { printf("\rNo SGB frame found.\n"); free(buf); free(best_out); return 2; }

    printf("\n%d windows synced — decoding best (z=%.1f)\n", found, (double)best_z);
    print_hex(best_out, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    printf("\n=== FRAME DECODING ===\n");
    decode_beacon(best_out, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    DEMODULATION COMPLETE                      ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    free(buf);
    free(best_out);
    return 0;
}
