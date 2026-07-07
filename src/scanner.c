#define _GNU_SOURCE
#include "scanner.h"
#include "dec406.h"
#include "diag_log.h"
#include "dsss_demod.h"
#include "fgb_iq_demod.h"
#include "scan_alert.h"
#include <complex.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NF_ALPHA 0.02
#define DET_FACTOR 7.0
#define SMOOTH_W 128
#define SMOOTH_FACTOR 1.4
#define MIN_CLUSTER 2
#define MAX_CLUSTER 600
#define CENTER_TOL 40
#define ON_FRAMES 4
#define OFF_FRAMES 16
#define WARMUP_FRAMES 64
#define BURST_AVG 24
#define BW_SPLIT_HZ 20000.0
#define BURST_BW_MAX 150000.0
#define FGB_DUR_MIN 0.35
#define FGB_DUR_MAX 0.80
#define SGB_DUR_MIN 0.80
#define SGB_DUR_MAX 1.25
#define HEARTBEAT_S 15

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_avail = PTHREAD_COND_INITIALIZER;

static const char *timestr(time_t t) {
    static char b[16];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(b, sizeof b, "%H:%M:%S", &tm);
    return b;
}

static const char *timestr_ms(void) {
    static char b[24];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    int n = (int)strftime(b, sizeof b, "%H:%M:%S", &tm);
    snprintf(b + n, sizeof b - n, ".%03ld", ts.tv_nsec / 1000000L);
    return b;
}

static void fft(float complex *x, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float complex t = x[i]; x[i] = x[j]; x[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        float complex wl = cosf(ang) + sinf(ang) * I;
        for (int i = 0; i < n; i += len) {
            float complex w = 1.0f;
            for (int k = 0; k < len / 2; k++) {
                float complex u = x[i + k];
                float complex v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

typedef void (*decode_print_fn)(const uint8_t *, int);
static char *capture_decode(decode_print_fn fn, const uint8_t *bits, int n) {
    fflush(stdout);
    FILE *tmp = tmpfile();
    if (!tmp) { fn(bits, n); return NULL; }
    int saved = dup(STDOUT_FILENO);
    if (saved < 0) { fclose(tmp); fn(bits, n); return NULL; }
    dup2(fileno(tmp), STDOUT_FILENO);
    fn(bits, n);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    fseek(tmp, 0, SEEK_END);
    long sz = ftell(tmp);
    rewind(tmp);
    char *buf = (sz >= 0) ? malloc((size_t)sz + 1) : NULL;
    if (buf) {
        size_t nread = fread(buf, 1, (size_t)sz, tmp);
        buf[nread] = '\0';
        fputs(buf, stdout);
    }
    fclose(tmp);
    return buf;
}

static int is_fgb_bench_test(const char *body) {
    return body &&
           strstr(body, "Protocol: 9 (ELT-DT Location Protocol)") &&
           strstr(body, "Identification: Aircraft 000000");
}

static int is_fgb_test_message(const char *body) {
    return body && strstr(body, "Test Protocol: Active");
}

static void decode_sgb(scanner_t *s, uint64_t start, uint64_t len,
                       double offset_hz, double snr_db) {
    uint64_t head = (uint64_t)(0.20 * s->samp_rate);
    uint64_t tail = (uint64_t)(0.20 * s->samp_rate);
    if (head > start) head = start;
    uint64_t ext_start = start - head;
    uint64_t ext_len = head + len + tail;

    pthread_mutex_lock(&lock);
    uint64_t wr = s->wr;
    pthread_mutex_unlock(&lock);
    if (ext_start + SCANNER_RING_SAMPLES <= wr) {
        DWARN("WARNING: SGB window overwritten\n");
        return;
    }
    if (ext_start + ext_len > wr) ext_len = wr - ext_start;

    float complex *win = malloc((size_t)ext_len * sizeof(float complex));
    if (!win) { DWARN("WARNING: SGB alloc failed\n"); return; }

    double w = 2.0 * M_PI * offset_hz / (double)s->samp_rate;
    for (uint64_t i = 0; i < ext_len; i++) {
        float complex sam = s->ring[(ext_start + i) & SCANNER_RING_MASK];
        double ph = w * (double)i;
        float complex lo = cosf(ph) - sinf(ph) * I;
        win[i] = sam * lo;
    }

    uint8_t bits[DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS];
    memset(bits, 0, sizeof bits);
    float z = 0.0f;
    float fs = (float)s->samp_rate;
    despread_prn_mode_t prn_mode = DESPREAD_PRN_NORMAL;
    int rc = dsss_receive_burst_ex(win, (size_t)ext_len, fs / 38400.0f, fs,
                                   0, bits, &z, &prn_mode);

    if (rc == 0) {
        if (getenv("DUMP_OK")) {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            char path[128];
            snprintf(path, sizeof path, "sgb_ok_%02d%02d%02d_%.0fHz.cf32",
                     tm->tm_hour, tm->tm_min, tm->tm_sec, offset_hz);
            FILE *fp = fopen(path, "wb");
            if (fp) {
                fwrite(win, sizeof(float complex), (size_t)ext_len, fp);
                fclose(fp);
                printf("  dumped %s\n", path);
            }
        }
        free(win);
        printf("  --- SGB frame decoded (z=%.1f, PRN=%s) ---\n",
               z, despread_prn_mode_name(prn_mode));
        char *body = capture_decode(decode_beacon, bits,
                                    DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
        double freq_mhz = (s->center_hz + offset_hz) / 1e6;
        int is_real = (prn_mode == DESPREAD_PRN_NORMAL &&
                       body && strstr(body, "Test Protocol: Normal Operation"));
        const char *hex_id = scan_alert_extract_hex_id(body);
        int is_repeat = scan_alert_is_repeat(hex_id);
        if (is_real && is_repeat && scan_alert_freq_authorised(freq_mhz))
            scan_alert_send("SGB", freq_mhz, snr_db, bits,
                            DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS, body);
        free(body);
    } else {
        printf("  SGB burst — decode failed (z=%.1f)\n", z);
        if (getenv("DUMP_FAIL")) {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            char path[128];
            snprintf(path, sizeof path, "burst_sgb_%02d%02d%02d_%.0fHz.cf32",
                     tm->tm_hour, tm->tm_min, tm->tm_sec, offset_hz);
            FILE *fp = fopen(path, "wb");
            if (fp) {
                fwrite(win, sizeof(float complex), (size_t)ext_len, fp);
                fclose(fp);
                printf("  dumped %s\n", path);
            }
        }
        free(win);
    }
    printf("\n");
    fflush(stdout);
}

static void decode_fgb(scanner_t *s, uint64_t start, uint64_t len,
                       double offset_hz, double snr_db) {
    uint64_t head = (uint64_t)(0.05 * s->samp_rate);
    uint64_t tail = (uint64_t)(0.10 * s->samp_rate);
    if (head > start) head = start;
    uint64_t ext_start = start - head;
    uint64_t ext_len = head + len + tail;

    pthread_mutex_lock(&lock);
    uint64_t wr = s->wr;
    pthread_mutex_unlock(&lock);
    if (ext_start + SCANNER_RING_SAMPLES <= wr) {
        DWARN("WARNING: FGB window overwritten\n");
        return;
    }
    if (ext_start + ext_len > wr) ext_len = wr - ext_start;

    float complex *win = malloc((size_t)ext_len * sizeof(float complex));
    if (!win) { DWARN("WARNING: FGB alloc failed\n"); return; }

    double w = 2.0 * M_PI * offset_hz / (double)s->samp_rate;
    for (uint64_t i = 0; i < ext_len; i++) {
        float complex sam = s->ring[(ext_start + i) & SCANNER_RING_MASK];
        double ph = w * (double)i;
        float complex lo = cosf(ph) - sinf(ph) * I;
        win[i] = sam * lo;
    }

    uint8_t bits[FGB_LONG_BITS];
    int rc = fgb_iq_decode(win, (size_t)ext_len, s->samp_rate, (long)head, bits);
    free(win);

    if (rc == 0) {
        char *body = capture_decode(decode_1g, bits, FGB_LONG_BITS);
        double freq_mhz = (s->center_hz + offset_hz) / 1e6;
        int is_orb = (body && strstr(body, "Identification: Orbitography"));
        const char *hex_id = scan_alert_extract_hex_id(body);
        int is_repeat = scan_alert_is_repeat(hex_id);
        int is_id_na = (body && strstr(body, "ID-NOT-AVAIL"));
        int is_bench = is_fgb_bench_test(body);
        int is_test = is_fgb_test_message(body);
        if (body && !is_orb && !is_id_na && !is_bench && !is_test && is_repeat &&
            scan_alert_freq_authorised(freq_mhz))
            scan_alert_send("FGB", freq_mhz, snr_db, bits, FGB_LONG_BITS, body);
        free(body);
    } else if (rc == -2) {
        printf("  FGB burst — CRC FAIL\n");
    } else {
        printf("  FGB burst — no frame decoded (no sync/burst)\n");
    }
    printf("\n");
    fflush(stdout);
}

static int measure_burst(scanner_t *s, uint64_t start, uint64_t len,
                         double *freq_off, double *bw_hz) {
    static double spec[SCANNER_FFT_N];
    static float complex win[SCANNER_FFT_N];

    pthread_mutex_lock(&lock);
    uint64_t wr = s->wr;
    pthread_mutex_unlock(&lock);
    if (start + SCANNER_RING_SAMPLES <= wr) return 0;

    for (int k = 0; k < SCANNER_FFT_N; k++) spec[k] = 0.0;

    int navg = (int)(len / SCANNER_FFT_N);
    if (navg < 1) navg = 1;
    if (navg > BURST_AVG) navg = BURST_AVG;
    uint64_t span = (len > (uint64_t)SCANNER_FFT_N) ? len - SCANNER_FFT_N : 0;
    uint64_t step = (navg > 1) ? span / (uint64_t)(navg - 1) : 0;

    for (int a = 0; a < navg; a++) {
        uint64_t base = start + (uint64_t)a * step;
        for (int i = 0; i < SCANNER_FFT_N; i++)
            win[i] = s->ring[(base + (uint64_t)i) & SCANNER_RING_MASK] *
                     (float)s->hann[i];
        fft(win, SCANNER_FFT_N);
        for (int k = 0; k < SCANNER_FFT_N; k++) {
            double re = crealf(win[k]), im = cimagf(win[k]);
            spec[k] += re * re + im * im;
        }
    }

    for (int k = 0; k < SCANNER_FFT_N; k++) {
        spec[k] -= (double)navg * s->floor_bin[k];
        if (spec[k] < 0.0) spec[k] = 0.0;
    }

    const double binhz = (double)s->samp_rate / SCANNER_FFT_N;
    double peak = 0.0;
    for (int k = 0; k < SCANNER_FFT_N; k++) {
        int ki = (k <= SCANNER_FFT_N / 2) ? k : k - SCANNER_FFT_N;
        double fa = s->center_hz + ki * binhz;
        if (abs(ki) <= s->dc_guard_bins || fa < s->f1 || fa > s->f2) continue;
        if (spec[k] > peak) peak = spec[k];
    }
    if (peak <= 0.0) return 0;

    double thr = peak * 0.1;
    double sum_p = 0.0, sum_fp = 0.0, min_off = 1e12, max_off = -1e12;
    for (int k = 0; k < SCANNER_FFT_N; k++) {
        int ki = (k <= SCANNER_FFT_N / 2) ? k : k - SCANNER_FFT_N;
        double fa = s->center_hz + ki * binhz;
        if (abs(ki) <= s->dc_guard_bins || fa < s->f1 || fa > s->f2 ||
            spec[k] < thr) continue;
        double f = ki * binhz;
        sum_p += spec[k];
        sum_fp += f * spec[k];
        if (f < min_off) min_off = f;
        if (f > max_off) max_off = f;
    }
    if (sum_p <= 0.0) return 0;

    *freq_off = sum_fp / sum_p;
    *bw_hz = max_off - min_off;
    if (*bw_hz > BURST_BW_MAX) return 0;
    return 1;
}

int scanner_init(scanner_t *s, uint32_t samp_rate, double center_hz,
                 double f1, double f2, int dc_guard_bins) {
    memset(s, 0, sizeof(*s));
    s->samp_rate = samp_rate;
    s->center_hz = center_hz;
    s->f1 = f1;
    s->f2 = f2;
    s->dc_guard_bins = dc_guard_bins;
    s->running = 1;

    s->ring = malloc((size_t)SCANNER_RING_SAMPLES * sizeof(float complex));
    if (!s->ring) return -1;
    memset(s->ring, 0, (size_t)SCANNER_RING_SAMPLES * sizeof(float complex));

    for (int k = 0; k < SCANNER_FFT_N; k++)
        s->hann[k] = 0.5 - 0.5 * cos(2.0 * M_PI * k / (SCANNER_FFT_N - 1));

    return 0;
}

void scanner_free(scanner_t *s) {
    if (s->ring) { free(s->ring); s->ring = NULL; }
}

void scanner_push(scanner_t *s, const float complex *samples, size_t n) {
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < n; i++) {
        s->ring[s->wr & SCANNER_RING_MASK] = samples[i];
        s->wr++;
    }
    pthread_cond_signal(&data_avail);
    pthread_mutex_unlock(&lock);
}

void scanner_stop(scanner_t *s) {
    s->running = 0;
    pthread_mutex_lock(&lock);
    pthread_cond_broadcast(&data_avail);
    pthread_mutex_unlock(&lock);
}

void scanner_process(scanner_t *s) {
    static float complex win[SCANNER_FFT_N];
    static double P[SCANNER_FFT_N];
    static unsigned char hotb[SCANNER_FFT_N];

    const double binhz = (double)s->samp_rate / SCANNER_FFT_N;
    uint64_t rd = 0, frame = 0;
    int state = 0, above = 0, below = 0;
    double cand_off = 0.0, burst_center = 0.0;
    uint64_t cand_start = 0, burst_start = 0;
    double bcenter_sum = 0.0, bsnr = 0.0;
    int bcenter_n = 0;
    time_t last_beat = time(NULL);

    uint64_t min_burst = (uint64_t)(0.20 * s->samp_rate);
    uint64_t max_burst = (uint64_t)(1.50 * s->samp_rate);

    while (s->running) {
        pthread_mutex_lock(&lock);
        while (s->running && (s->wr - rd) < SCANNER_FFT_N)
            pthread_cond_wait(&data_avail, &lock);
        uint64_t avail = s->wr - rd;
        pthread_mutex_unlock(&lock);
        if (avail < SCANNER_FFT_N) break;

        if (avail > SCANNER_RING_SAMPLES) {
            s->overruns++;
            DWARN("WARNING: ring overrun (%lu)\n", s->overruns);
            rd = s->wr - SCANNER_RING_SAMPLES;
            state = 0; above = 0;
        }

        uint64_t fstart = rd;
        for (int i = 0; i < SCANNER_FFT_N; i++)
            win[i] = s->ring[(fstart + (uint64_t)i) & SCANNER_RING_MASK] *
                     (float)s->hann[i];
        fft(win, SCANNER_FFT_N);
        for (int k = 0; k < SCANNER_FFT_N; k++) {
            double re = crealf(win[k]), im = cimagf(win[k]);
            P[k] = re * re + im * im;
        }
        rd += SCANNER_FFT_N;
        frame++;
        if (frame == 1)
            for (int k = 0; k < SCANNER_FFT_N; k++) s->floor_bin[k] = P[k];

        static double pref[SCANNER_FFT_N + 1], preff[SCANNER_FFT_N + 1];
        pref[0] = preff[0] = 0.0;
        for (int k = 0; k < SCANNER_FFT_N; k++) {
            pref[k + 1] = pref[k] + P[k];
            preff[k + 1] = preff[k] + s->floor_bin[k];
        }
        for (int k = 0; k < SCANNER_FFT_N; k++) {
            int ki = (k <= SCANNER_FFT_N / 2) ? k : k - SCANNER_FFT_N;
            if (abs(ki) <= s->dc_guard_bins) { hotb[k] = 0; continue; }
            int raw_hot = P[k] > s->floor_bin[k] * DET_FACTOR;
            int lo = k - SMOOTH_W / 2, hi = k + SMOOTH_W / 2;
            if (lo < 0) lo = 0;
            if (hi > SCANNER_FFT_N - 1) hi = SCANNER_FFT_N - 1;
            int smooth_hot = (pref[hi + 1] - pref[lo]) >
                             (preff[hi + 1] - preff[lo]) * SMOOTH_FACTOR;
            hotb[k] = (raw_hot || smooth_hot) ? 1 : 0;
        }
        if (state == 0)
            for (int k = 0; k < SCANNER_FFT_N; k++)
                if (!hotb[k])
                    s->floor_bin[k] += NF_ALPHA * (P[k] - s->floor_bin[k]);

        int best_lo = -1, best_hi = -1;
        double best_pwr = 0.0;
        for (int k = 0; k < SCANNER_FFT_N;) {
            if (!hotb[k]) { k++; continue; }
            int lo = k;
            double pw = 0.0;
            while (k < SCANNER_FFT_N && hotb[k]) { pw += P[k]; k++; }
            int width = (k - 1) - lo + 1;
            double cb = 0.5 * (lo + (k - 1));
            if (cb > SCANNER_FFT_N / 2) cb -= SCANNER_FFT_N;
            double cfreq = s->center_hz + cb * binhz;
            if (width >= MIN_CLUSTER && width <= MAX_CLUSTER && pw > best_pwr &&
                cfreq >= s->f1 && cfreq <= s->f2) {
                best_pwr = pw; best_lo = lo; best_hi = k - 1;
            }
        }

        int have = (best_lo >= 0);
        double off = 0.0;
        if (have) {
            double sw = 0.0, swk = 0.0;
            for (int k = best_lo; k <= best_hi; k++) {
                int ki = (k <= SCANNER_FFT_N / 2) ? k : k - SCANNER_FFT_N;
                sw += P[k]; swk += (double)ki * P[k];
            }
            if (sw > 0.0) off = swk / sw * binhz;
        }

        if (state == 0 && frame > WARMUP_FRAMES) {
            if (have) {
                if (!(above > 0 && fabs(off - cand_off) < CENTER_TOL * binhz)) {
                    above = 0; cand_start = fstart;
                }
                above++; cand_off = off;
                if (above >= ON_FRAMES) {
                    state = 1; burst_start = cand_start; burst_center = cand_off;
                    below = 0; bcenter_sum = 0.0; bcenter_n = 0; bsnr = 0.0;
                }
            } else {
                above = 0;
            }
        }

        if (state == 1) {
            int near = have && fabs(off - burst_center) < CENTER_TOL * binhz;
            if (!near) {
                int cbin = (int)(burst_center / binhz + 0.5);
                if (cbin < 0) cbin += SCANNER_FFT_N;
                int bw_bins = 20;
                double sig = 0.0, fl = 0.0;
                for (int dk = -bw_bins; dk <= bw_bins; dk++) {
                    int k = (cbin + dk + SCANNER_FFT_N) % SCANNER_FFT_N;
                    sig += P[k]; fl += s->floor_bin[k];
                }
                if (fl > 0.0 && sig > fl * 2.0) near = 1;
            }
            if (near) {
                below = 0;
                bcenter_sum += off; bcenter_n++;
                burst_center = bcenter_sum / bcenter_n;
                double fl = 0.0;
                for (int k = best_lo; k <= best_hi; k++) fl += s->floor_bin[k];
                if (fl > 0.0) {
                    double snr = 10.0 * log10(best_pwr / fl);
                    if (snr > bsnr) bsnr = snr;
                }
            } else {
                below++;
            }
            uint64_t cur_len = rd - burst_start;
            if (below >= OFF_FRAMES || cur_len > max_burst) {
                uint64_t bend = rd - (uint64_t)below * SCANNER_FFT_N;
                uint64_t blen = bend - burst_start;
                double fmeas, bwmeas;
                if (blen >= min_burst && blen <= max_burst &&
                    measure_burst(s, burst_start, blen, &fmeas, &bwmeas)) {
                    const char *type = (bwmeas > BW_SPLIT_HZ) ? "SGB" : "FGB";
                    double dur_s = (double)blen / s->samp_rate;
                    int is_sgb = (bwmeas > BW_SPLIT_HZ);
                    int dur_ok = is_sgb
                        ? (dur_s >= SGB_DUR_MIN && dur_s <= SGB_DUR_MAX)
                        : (dur_s >= FGB_DUR_MIN && dur_s <= FGB_DUR_MAX);
                    static uint64_t prev_burst_start = 0;
                    double dt = (prev_burst_start && burst_start >= prev_burst_start)
                        ? (double)(burst_start - prev_burst_start) / s->samp_rate
                        : 0.0;
                    prev_burst_start = burst_start;
                    if (!dur_ok) {
                        DIAG("[%s] REJECT %.4f MHz   BW ~%3.0f kHz   %s   "
                             "dur %.2f s (out of range)\n",
                             timestr_ms(), (s->center_hz + fmeas) / 1e6,
                             bwmeas / 1e3, type, dur_s);
                    } else {
                        printf("[%s] BURST  %.4f MHz   BW ~%3.0f kHz   %s   "
                               "SNR %2.0f dB   dur %.2f s   dt %.3f s\n",
                               timestr_ms(), (s->center_hz + fmeas) / 1e6,
                               bwmeas / 1e3, type, bsnr, dur_s, dt);
                        fflush(stdout);
                        if (is_sgb)
                            decode_sgb(s, burst_start, blen, fmeas, bsnr);
                        else
                            decode_fgb(s, burst_start, blen, fmeas, bsnr);
                    }
                }
                state = 0; above = 0;
                for (int k = 0; k < SCANNER_FFT_N; k++)
                    if (hotb[k]) s->floor_bin[k] = P[k];
            }
        }

        time_t now = time(NULL);
        if (now - last_beat >= HEARTBEAT_S) {
            last_beat = now;
            double fl = 0.0;
            for (int k = 0; k < SCANNER_FFT_N; k++) fl += s->floor_bin[k];
            DIAG("[%s] monitoring — mean noise floor %.2e, overruns %lu\n",
                 timestr(now), fl / SCANNER_FFT_N, s->overruns);
        }
    }
}
