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

static void print_usage(const char *p) {
    printf("Usage: %s <file> [-s rate] [-f freq] [-u] [-i]\n", p);
    printf("  -s  Sample rate (default 2457600)\n");
    printf("  -f  Frequency offset in Hz\n");
    printf("  -u  RTL-SDR uint8 input\n");
    printf("  -i  int16 input\n");
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

static size_t read_win(FILE *fp, int u8, int i16, size_t n, float complex *dst) {
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
    return fread(dst, sizeof(float complex), n, fp);
}

int main(int argc, char *argv[]) {
    const char *fn = NULL;
    float fs = 2457600.0f, foff = 0.0f;
    int u8 = 0, i16 = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"-s") && i+1<argc) fs = atof(argv[++i]);
        else if (!strcmp(argv[i],"-f") && i+1<argc) foff = atof(argv[++i]);
        else if (!strcmp(argv[i],"-u")) u8 = 1;
        else if (!strcmp(argv[i],"-i")) i16 = 1;
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

    for (size_t off = 0; off + win <= total; off += step) {
        fseek(fp, (long)(off * bps), SEEK_SET);
        size_t n = read_win(fp, u8, i16, win, buf);
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

        if (dsss_receive_burst(buf, win, sps, fs, 0, out) == 0) {
            printf("\r  Sync at t=%.2fs\n", off/fs);
            found++;
        }
    }
    fclose(fp);

    if (!found) { printf("\rNo SGB frame found.\n"); free(buf); return 2; }

    printf("\n%d windows synced — decoding last\n", found);
    print_hex(out, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    printf("\n=== FRAME DECODING ===\n");
    decode_beacon(out, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    DEMODULATION COMPLETE                      ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    free(buf);
    return 0;
}
