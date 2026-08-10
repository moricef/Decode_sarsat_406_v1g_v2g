/**
 * @file main_fgb_iq.c
 * @brief COSPAS-SARSAT 1G (FGB) IQ demodulator. Offline counterpart of
 *        dec406_iq, which handles 2G only.
 *
 * Replays a recorded IQ file through the same IQ-direct FGB chain the
 * real-time scanner uses (fgb_iq_decode), so captures can be re-analysed
 * and regression-tested without radio hardware.
 *
 * Usage: ./dec406_fgb_iq <file> [-s rate] [-f freq] [-u] [-i] [-I]
 *   -u  RTL-SDR uint8 input
 *   -i  int16 interleaved input
 *   -I  int32 input (SDRangel ci32_le)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>
#include "fgb_iq_demod.h"
#include "dec406.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Burst gating, mirrors the scanner's FGB duration window */
#define FGB_DUR_MIN   0.35
#define FGB_DUR_MAX   0.80
#define WIN_SEC       3.0       /* processing window */
#define OVL_SEC       1.0       /* overlap, must exceed FGB_DUR_MAX */
#define HEAD_SEC      0.05      /* pre-roll handed to the demodulator */
#define TAIL_SEC      0.10

/* An FGB occupies only ~4 kHz. Detecting it on wideband power fails: in a
 * 2.5 MHz stream the burst is diluted by ~28 dB and sits below the noise.
 * Detection therefore runs per channel — mix the candidate carrier to zero,
 * decimate hard, and look for a power step in that narrow band. */
#define NB_HZ         4000.0    /* detection bandwidth */
#define PROF_MS       10.0      /* power profile granularity */
#define SCAN_FFT      8192      /* channel search resolution */
#define MAX_CHAN      16

static int cmp_float(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

static size_t read_win(FILE *fp, int u8, int i16, int i32, size_t n,
                       float complex *dst) {
    if (u8) {
        unsigned char *r = malloc(n * 2);
        if (!r) return 0;
        size_t k = fread(r, 2, n, fp);
        for (size_t i = 0; i < k; i++)
            dst[i] = ((float)r[2*i]-127.5f)/127.5f
                   + ((float)r[2*i+1]-127.5f)/127.5f * I;
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

/* Survey the spectrum and return candidate carrier offsets, strongest first.
 * Max-hold over the whole window rather than an average: an FGB burst lasts
 * ~0.5 s and would be washed out by averaging over several seconds.
 * FGB carriers sit ~28 kHz below the SGB channel because the GEOSAR repeater
 * is a transparent translator: 406.022 MHz and 406.05 MHz keep their spacing
 * on the 1544 MHz downlink. */
static int find_channels(const float complex *iq, long n, int samp_rate,
                         double *out, int max_out) {
    int nb = SCAN_FFT;
    if (n < nb) return 0;

    fftwf_complex *in  = fftwf_alloc_complex((size_t)nb);
    fftwf_complex *sp  = fftwf_alloc_complex((size_t)nb);
    float *hold = calloc((size_t)nb, sizeof(float));
    float *win  = malloc((size_t)nb * sizeof(float));
    if (!in || !sp || !hold || !win) {
        if (in) fftwf_free(in); if (sp) fftwf_free(sp);
        free(hold); free(win);
        return 0;
    }
    fftwf_plan pl = fftwf_plan_dft_1d(nb, in, sp, FFTW_FORWARD, FFTW_ESTIMATE);
    for (int i = 0; i < nb; i++)
        win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (nb - 1));

    for (long base = 0; base + nb <= n; base += nb) {
        for (int i = 0; i < nb; i++)
            in[i] = iq[base + i] * win[i];
        fftwf_execute(pl);
        for (int k = 0; k < nb; k++) {
            float re = crealf(sp[k]), im = cimagf(sp[k]);
            float p = re * re + im * im;
            if (p > hold[k]) hold[k] = p;
        }
    }
    fftwf_destroy_plan(pl);
    fftwf_free(in); fftwf_free(sp); free(win);

    float *srt = malloc((size_t)nb * sizeof(float));
    if (!srt) { free(hold); return 0; }
    memcpy(srt, hold, (size_t)nb * sizeof(float));
    qsort(srt, (size_t)nb, sizeof(float), cmp_float);
    float med = srt[nb / 2];
    free(srt);

    double binhz = (double)samp_rate / nb;
    int mask = (int)(3000.0 / binhz);       /* +/-3 kHz: FGB channels are only a few kHz apart */
    int cg   = (int)(2000.0 / binhz);       /* centroid half-width */
    int found = 0;
    while (found < max_out) {
        int best = -1; float bp = med * 4.0f;
        for (int k = 0; k < nb; k++) if (hold[k] > bp) { bp = hold[k]; best = k; }
        if (best < 0) break;

        /* The strongest bin is not necessarily the channel centre: take the
         * energy centroid around it, otherwise a few kHz of offset pushes the
         * carrier out of the narrow detection band and the burst is missed. */
        double num = 0.0, den = 0.0;
        for (int k = best - cg; k <= best + cg; k++) {
            int idx = ((k % nb) + nb) % nb;
            double e = hold[idx] - med;
            if (e <= 0.0) continue;
            num += e * (double)k;
            den += e;
        }
        double kc = (den > 0.0) ? num / den : (double)best;
        double kk = (kc < nb / 2.0) ? kc : kc - nb;   /* unwrap to +/- */
        out[found++] = kk * binhz;

        for (int k = best - mask; k <= best + mask; k++) {
            int idx = ((k % nb) + nb) % nb;
            hold[idx] = 0.0f;
        }
    }
    free(hold);
    return found;
}

static void print_usage(const char *p) {
    printf("Usage: %s <file> [-s rate] [-f freq] [-u] [-i] [-I]\n", p);
    printf("  -s  Sample rate (default 2457600)\n");
    printf("  -f  Fixed frequency offset in Hz (skips estimation)\n");
    printf("  -u  RTL-SDR uint8 input\n");
    printf("  -i  int16 input\n");
    printf("  -I  int32 input (SDRangel ci32_le)\n");
}

int main(int argc, char *argv[]) {
    /* Line-buffer stdout: it is block-buffered on a pipe while stderr never
     * is, which reorders output under `2>&1 | tee`. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *fn = NULL;
    int samp_rate = 2457600;
    int u8 = 0, i16 = 0, i32 = 0;
    double fixed_off = 0.0;
    int have_fixed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") && i + 1 < argc) samp_rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
            fixed_off = atof(argv[++i]); have_fixed = 1;
        }
        else if (!strcmp(argv[i], "-u")) u8 = 1;
        else if (!strcmp(argv[i], "-i")) i16 = 1;
        else if (!strcmp(argv[i], "-I")) i32 = 1;
        else if (argv[i][0] != '-') fn = argv[i];
        else { print_usage(argv[0]); return 1; }
    }
    if (!fn) { print_usage(argv[0]); return 1; }

    FILE *fp = fopen(fn, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", fn); return 1; }

    size_t win_n = (size_t)(WIN_SEC * samp_rate);
    size_t ovl_n = (size_t)(OVL_SEC * samp_rate);
    long   head  = (long)(HEAD_SEC * samp_rate);
    long   tail  = (long)(TAIL_SEC * samp_rate);

    float complex *buf = malloc(win_n * sizeof(float complex));
    if (!buf) { fprintf(stderr, "alloc failed\n"); fclose(fp); return 1; }

    int dec = (int)((double)samp_rate / NB_HZ + 0.5);   /* -> ~4 kHz */
    if (dec < 1) dec = 1;
    long prof_w = (long)(PROF_MS / 1000.0 * samp_rate / dec);
    if (prof_w < 1) prof_w = 1;

    double chan[MAX_CHAN];
    int n_chan = 0;

    size_t filled = 0;
    uint64_t abs_base = 0;          /* absolute sample index of buf[0] */
    uint64_t last_decoded_end = 0;  /* dedup across the overlap */
    int n_burst = 0, n_ok = 0, n_crc = 0, n_nof = 0;

    printf("FGB offline decoder — %s @ %d Hz\n", fn, samp_rate);

    while (1) {
        size_t got = read_win(fp, u8, i16, i32, win_n - filled, buf + filled);
        filled += got;
        if (filled < win_n / 4) break;

        /* One-off channel survey on the first full window */
        if (n_chan == 0) {
            if (have_fixed) { chan[0] = fixed_off; n_chan = 1; }
            else n_chan = find_channels(buf, (long)filled, samp_rate,
                                        chan, MAX_CHAN);
            printf("channels: ");
            for (int c = 0; c < n_chan; c++) printf("%+.1f kHz  ", chan[c] / 1e3);
            printf("\n");
            if (n_chan == 0) { printf("no carrier found\n"); break; }
        }

        size_t nnb = filled / (size_t)dec;
        long nprof = (long)(nnb / (size_t)prof_w);
        float complex *nb = malloc(nnb * sizeof(float complex));
        float *prof = malloc((size_t)(nprof > 0 ? nprof : 1) * sizeof(float));
        float *srt  = malloc((size_t)(nprof > 0 ? nprof : 1) * sizeof(float));

        if (nb && prof && srt && nprof >= 8) {
          for (int c = 0; c < n_chan; c++) {
            /* Mix this channel to zero and decimate to the detection band */
            double w0 = -2.0 * M_PI * chan[c] / (double)samp_rate;
            for (size_t k = 0; k < nnb; k++) {
                float complex acc = 0.0f;
                size_t base = k * (size_t)dec;
                for (int d = 0; d < dec; d++) {
                    double ph = w0 * (double)(base + (size_t)d);
                    acc += buf[base + (size_t)d]
                         * (float complex)(cos(ph) + sin(ph) * I);
                }
                nb[k] = acc / (float)dec;
            }
            for (long b = 0; b < nprof; b++) {
                float s = 0.0f;
                for (long j = 0; j < prof_w; j++) {
                    float complex v = nb[(size_t)(b * prof_w + j)];
                    s += crealf(v) * crealf(v) + cimagf(v) * cimagf(v);
                }
                prof[b] = s / (float)prof_w;
            }
            memcpy(srt, prof, (size_t)nprof * sizeof(float));
            qsort(srt, (size_t)nprof, sizeof(float), cmp_float);
            float med = srt[nprof / 2];
            if (med <= 0.0f) continue;
            float thr = med * 3.0f;

            for (long b = 0; b < nprof; b++) {
                if (prof[b] <= thr) continue;
                long b0 = b;
                while (b < nprof && prof[b] > thr) b++;
                long b1 = b;

                double dur = (double)(b1 - b0) * PROF_MS / 1000.0;
                if (dur < FGB_DUR_MIN || dur > FGB_DUR_MAX) continue;

                long bstart = (long)(b0 * prof_w * (long)dec);
                long blen   = (long)((b1 - b0) * prof_w * (long)dec);
                uint64_t abs_start = abs_base + (uint64_t)bstart;
                if (abs_start < last_decoded_end) continue;  /* overlap dup */

                long ext_start = bstart - head;
                if (ext_start < 0) ext_start = 0;
                long ext_len = (bstart - ext_start) + blen + tail;
                if (ext_start + ext_len > (long)filled)
                    ext_len = (long)filled - ext_start;
                if (ext_len < blen) continue;

                float complex *win = malloc((size_t)ext_len
                                            * sizeof(float complex));
                if (!win) continue;
                double w = 2.0 * M_PI * chan[c] / (double)samp_rate;
                for (long i = 0; i < ext_len; i++) {
                    double ph = w * (double)(ext_start + i);
                    win[i] = buf[ext_start + i]
                           * (float complex)(cos(ph) - sin(ph) * I);
                }

                n_burst++;
                printf("\n[%.3f s] BURST  %+.1f kHz  dur %.2f s  pk/med %.1f\n",
                       (double)abs_start / samp_rate, chan[c] / 1e3, dur,
                       (double)(prof[b0] / med));
                fflush(stdout);

                uint8_t bits[FGB_LONG_BITS];
                int flen = 0;
                int rc = fgb_iq_decode(win, (size_t)ext_len, samp_rate,
                                       bstart - ext_start, bits, &flen);
                free(win);

                if (rc == 0)       { n_ok++;  decode_1g(bits, flen); }
                else if (rc == -2) { n_crc++; printf("  FGB burst — CRC FAIL\n"); }
                else               { n_nof++; printf("  FGB burst — no frame decoded\n"); }

                last_decoded_end = abs_start + (uint64_t)blen;
            }
          }
        }
        free(nb); free(prof); free(srt);

        if (got == 0) break;

        /* Slide the window, keeping the overlap so a burst straddling the
         * boundary is still seen whole on the next pass. */
        if (filled > ovl_n) {
            memmove(buf, buf + (filled - ovl_n), ovl_n * sizeof(float complex));
            abs_base += (uint64_t)(filled - ovl_n);
            filled = ovl_n;
        }
    }

    printf("\n=== %d burst(s): %d decoded, %d CRC fail, %d no frame ===\n",
           n_burst, n_ok, n_crc, n_nof);

    free(buf);
    fclose(fp);
    return 0;
}
