/**
 * @file main_scan_airspy.c
 * @brief dec406_scan_airspy — real-time FGB+SGB scanner using Airspy Mini.
 *
 * Same spectral burst detector and FGB/SGB decoders as main_scan.c,
 * with the Airspy Mini as RF front-end instead of RTL-SDR.
 *
 * Key differences from the RTL-SDR variant:
 *   - float32 IQ samples from Airspy (12-bit ADC, not 8-bit)
 *   - ring buffer stores float complex directly (no uint8 conversion)
 *   - async callback instead of sync reads
 *   - sample rate: 3 MSPS (closest available to 2.4576 MSPS)
 *
 * Usage: dec406_scan_airspy <freq_start> <freq_end> [gain] [bias_tee]
 *   e.g. dec406_scan_airspy 1544.05M 1544.15M
 *        dec406_scan_airspy 406.0M 406.1M 15
 *        dec406_scan_airspy 1544.05M 1544.15M 15 1
 */

#define _GNU_SOURCE
#include "audio_capture.h"
#include "dec406.h"
#include "diag_log.h"
#include "dsss_demod.h"
#include "fgb_iq_demod.h"
#include "scan_alert.h"
#include <complex.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libairspy/airspy.h>

/* Airspy Mini: 3 MSPS or 6 MSPS.  3 MSPS keeps processing light. */
static uint32_t samp_rate = 3000000u;

#define RING_BITS 24
#define RING_SAMPLES (1u << RING_BITS)   /* ~5.6 s at 3 MSPS */
#define RING_MASK (RING_SAMPLES - 1u)

#define FFT_N 8192       /* 366 Hz/bin at 3 MSPS */
#define DC_GUARD_BINS 4  /* Airspy has very clean DC — fewer guard bins */
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
/* T.001 §2.2.2: FGB 440/520 ms.  T.018 §2.3: SGB 1000 ms. */
#define FGB_DUR_MIN 0.35
#define FGB_DUR_MAX 0.80
#define SGB_DUR_MIN 0.80
#define SGB_DUR_MAX 1.25
#define HEARTBEAT_S 15

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t sigint_recvd = 0;
static void on_sigint(int s) { (void)s; running = 0; sigint_recvd = 1; }

static float complex *ring = NULL;
static uint64_t g_wr = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_avail = PTHREAD_COND_INITIALIZER;
static struct airspy_device *adev = NULL;
static unsigned long overruns = 0;
static double g_center_hz = 0.0;
static double g_f1 = 0.0, g_f2 = 0.0;
static double hann[FFT_N];
static double floor_bin[FFT_N];

static double parse_freq(const char *s) {
  char *end;
  double v = strtod(s, &end);
  switch (*end) {
  case 'k': case 'K': v *= 1e3; break;
  case 'M': v *= 1e6; break;
  case 'G': v *= 1e9; break;
  }
  return v;
}

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

/* ── decode helpers (same logic as main_scan.c, ring is float complex) ── */

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

static void decode_sgb(uint64_t start, uint64_t len, double offset_hz, double snr_db) {
  uint64_t head = (uint64_t)(0.20 * samp_rate);
  uint64_t tail = (uint64_t)(0.20 * samp_rate);
  if (head > start) head = start;
  uint64_t ext_start = start - head;
  uint64_t ext_len = head + len + tail;

  pthread_mutex_lock(&lock);
  uint64_t wr = g_wr;
  pthread_mutex_unlock(&lock);
  if (ext_start + RING_SAMPLES <= wr) {
    DWARN("WARNING: SGB window overwritten before decode\n");
    return;
  }
  if (ext_start + ext_len > wr) ext_len = wr - ext_start;

  float complex *win = malloc((size_t)ext_len * sizeof(float complex));
  if (!win) { DWARN("WARNING: SGB window alloc failed\n"); return; }

  double w = 2.0 * M_PI * offset_hz / (double)samp_rate;
  for (uint64_t i = 0; i < ext_len; i++) {
    float complex s = ring[(ext_start + i) & RING_MASK];
    double ph = w * (double)i;
    float complex lo = cosf(ph) - sinf(ph) * I;
    win[i] = s * lo;
  }

  uint8_t bits[DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS];
  memset(bits, 0, sizeof bits);
  float z = 0.0f;
  float fs = (float)samp_rate;
  despread_prn_mode_t prn_mode = DESPREAD_PRN_NORMAL;
  int rc = dsss_receive_burst_ex(win, (size_t)ext_len, fs / 38400.0f, fs,
                                 0, bits, &z, &prn_mode);

  if (rc == 0) {
    free(win);
    printf("  --- SGB frame decoded (z=%.1f, PRN=%s) ---\n",
           z, despread_prn_mode_name(prn_mode));
    decode_2g_set_frame_mode(prn_mode == DESPREAD_PRN_SELF_TEST);
    char *body = capture_decode(decode_beacon, bits, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    double freq_mhz = (g_center_hz + offset_hz) / 1e6;
    int is_real_distress = (body && strstr(body, "Test Protocol: Normal Operation") != NULL);
    const char *hex_id = scan_alert_extract_hex_id(body);
    int is_repeat = scan_alert_is_repeat(hex_id);
    if (is_real_distress && is_repeat && scan_alert_channel_allows(freq_mhz, body))
      scan_alert_send("SGB", freq_mhz, snr_db, bits, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS, body);
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
        printf("  dumped %s (%lu samples, %.3f MHz)\n",
               path, (unsigned long)ext_len, (g_center_hz + offset_hz) / 1e6);
      }
    }
    free(win);
  }
  printf("\n");
  fflush(stdout);
}

static void decode_fgb(uint64_t start, uint64_t len, double offset_hz, double snr_db) {
  uint64_t head = (uint64_t)(0.05 * samp_rate);
  uint64_t tail = (uint64_t)(0.10 * samp_rate);
  if (head > start) head = start;
  uint64_t ext_start = start - head;
  uint64_t ext_len = head + len + tail;

  pthread_mutex_lock(&lock);
  uint64_t wr = g_wr;
  pthread_mutex_unlock(&lock);
  if (ext_start + RING_SAMPLES <= wr) {
    DWARN("WARNING: FGB window overwritten before decode\n");
    return;
  }
  if (ext_start + ext_len > wr) ext_len = wr - ext_start;

  float complex *win = malloc((size_t)ext_len * sizeof(float complex));
  if (!win) { DWARN("WARNING: FGB window alloc failed\n"); return; }

  double w = 2.0 * M_PI * offset_hz / (double)samp_rate;
  for (uint64_t i = 0; i < ext_len; i++) {
    float complex s = ring[(ext_start + i) & RING_MASK];
    double ph = w * (double)i;
    float complex lo = cosf(ph) - sinf(ph) * I;
    win[i] = s * lo;
  }

  uint8_t bits[FGB_LONG_BITS];
  int rc = fgb_iq_decode(win, (size_t)ext_len, samp_rate, (long)head, bits);
  free(win);

  if (rc == 0) {
    char *body = capture_decode(decode_1g, bits, FGB_LONG_BITS);
    double freq_mhz = (g_center_hz + offset_hz) / 1e6;
    int is_orbitography = (body && strstr(body, "Identification: Orbitography") != NULL);
    const char *hex_id = scan_alert_extract_hex_id(body);
    int is_repeat = scan_alert_is_repeat(hex_id);
    int is_id_not_avail = (body && strstr(body, "ID-NOT-AVAIL") != NULL);
    int is_bench_test = is_fgb_bench_test(body);
    int is_test_message = is_fgb_test_message(body);
    if (body && !is_orbitography && !is_id_not_avail && !is_bench_test &&
        !is_test_message && is_repeat &&
        scan_alert_channel_allows(freq_mhz, body))
      scan_alert_send("FGB", freq_mhz, snr_db, bits, FGB_LONG_BITS, body);
    free(body);
  } else if (rc == -2) {
    printf("  FGB burst — CRC FAIL\n");
  } else {
    printf("  FGB burst — no frame decoded\n");
  }
  printf("\n");
  fflush(stdout);
}

/* ── Airspy callback — writes float32 IQ into the ring ── */

static int airspy_rx_callback(airspy_transfer *transfer) {
  if (!running) return -1;

  float *src = (float *)transfer->samples;
  int count = transfer->sample_count;

  pthread_mutex_lock(&lock);
  for (int i = 0; i < count; i++) {
    size_t idx = (size_t)(g_wr & RING_MASK);
    ring[idx] = src[2*i] + src[2*i + 1] * I;
    g_wr++;
  }
  pthread_cond_signal(&data_avail);
  pthread_mutex_unlock(&lock);
  return 0;
}

/* ── burst measurement (ring is float complex) ── */

static int measure_burst(uint64_t start, uint64_t len, double *freq_off, double *bw_hz) {
  static double spec[FFT_N];
  static float complex win[FFT_N];

  pthread_mutex_lock(&lock);
  uint64_t wr = g_wr;
  pthread_mutex_unlock(&lock);
  if (start + RING_SAMPLES <= wr) return 0;

  for (int k = 0; k < FFT_N; k++) spec[k] = 0.0;

  int navg = (int)(len / FFT_N);
  if (navg < 1) navg = 1;
  if (navg > BURST_AVG) navg = BURST_AVG;
  uint64_t span = (len > (uint64_t)FFT_N) ? len - FFT_N : 0;
  uint64_t step = (navg > 1) ? span / (uint64_t)(navg - 1) : 0;

  for (int a = 0; a < navg; a++) {
    uint64_t base = start + (uint64_t)a * step;
    for (int i = 0; i < FFT_N; i++)
      win[i] = ring[(base + (uint64_t)i) & RING_MASK] * (float)hann[i];
    fft(win, FFT_N);
    for (int k = 0; k < FFT_N; k++) {
      double re = crealf(win[k]), im = cimagf(win[k]);
      spec[k] += re * re + im * im;
    }
  }

  for (int k = 0; k < FFT_N; k++) {
    spec[k] -= (double)navg * floor_bin[k];
    if (spec[k] < 0.0) spec[k] = 0.0;
  }

  const double binhz = (double)samp_rate / FFT_N;
  double peak = 0.0;
  for (int k = 0; k < FFT_N; k++) {
    int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
    double fa = g_center_hz + ki * binhz;
    if (abs(ki) <= DC_GUARD_BINS || fa < g_f1 || fa > g_f2) continue;
    if (spec[k] > peak) peak = spec[k];
  }
  if (peak <= 0.0) return 0;

  double thr = peak * 0.1;
  double sum_p = 0.0, sum_fp = 0.0, min_off = 1e12, max_off = -1e12;
  for (int k = 0; k < FFT_N; k++) {
    int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
    double fa = g_center_hz + ki * binhz;
    if (abs(ki) <= DC_GUARD_BINS || fa < g_f1 || fa > g_f2 || spec[k] < thr) continue;
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

/* ── processing thread (identical logic, reads float complex ring) ── */

static void *process_thread(void *arg) {
  (void)arg;
  static float complex win[FFT_N];
  static double P[FFT_N];
  static unsigned char hotb[FFT_N];

  const double binhz = (double)samp_rate / FFT_N;
  uint64_t rd = 0, frame = 0;
  int state = 0;
  int above = 0, below = 0;
  double cand_off = 0.0, burst_center = 0.0;
  uint64_t cand_start = 0, burst_start = 0;
  double bcenter_sum = 0.0, bsnr = 0.0;
  int bcenter_n = 0;
  time_t last_beat = time(NULL);

  uint64_t min_burst = (uint64_t)(0.20 * samp_rate);
  uint64_t max_burst = (uint64_t)(1.50 * samp_rate);

  while (running) {
    pthread_mutex_lock(&lock);
    while (running && (g_wr - rd) < FFT_N)
      pthread_cond_wait(&data_avail, &lock);
    uint64_t avail = g_wr - rd;
    pthread_mutex_unlock(&lock);
    if (avail < FFT_N) break;

    if (avail > RING_SAMPLES) {
      overruns++;
      DWARN("WARNING: ring overrun (%lu)\n", overruns);
      rd = g_wr - RING_SAMPLES;
      state = 0; above = 0;
    }

    uint64_t fstart = rd;
    for (int i = 0; i < FFT_N; i++)
      win[i] = ring[(fstart + (uint64_t)i) & RING_MASK] * (float)hann[i];
    fft(win, FFT_N);
    for (int k = 0; k < FFT_N; k++) {
      double re = crealf(win[k]), im = cimagf(win[k]);
      P[k] = re * re + im * im;
    }
    rd += FFT_N;
    frame++;
    if (frame == 1)
      for (int k = 0; k < FFT_N; k++) floor_bin[k] = P[k];

    static double pref[FFT_N + 1], preff[FFT_N + 1];
    pref[0] = preff[0] = 0.0;
    for (int k = 0; k < FFT_N; k++) {
      pref[k + 1] = pref[k] + P[k];
      preff[k + 1] = preff[k] + floor_bin[k];
    }
    for (int k = 0; k < FFT_N; k++) {
      int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
      if (abs(ki) <= DC_GUARD_BINS) { hotb[k] = 0; continue; }
      int raw_hot = P[k] > floor_bin[k] * DET_FACTOR;
      int lo = k - SMOOTH_W / 2, hi = k + SMOOTH_W / 2;
      if (lo < 0) lo = 0;
      if (hi > FFT_N - 1) hi = FFT_N - 1;
      int smooth_hot = (pref[hi + 1] - pref[lo]) >
                       (preff[hi + 1] - preff[lo]) * SMOOTH_FACTOR;
      hotb[k] = (raw_hot || smooth_hot) ? 1 : 0;
    }
    if (state == 0)
      for (int k = 0; k < FFT_N; k++)
        if (!hotb[k]) floor_bin[k] += NF_ALPHA * (P[k] - floor_bin[k]);

    int best_lo = -1, best_hi = -1;
    double best_pwr = 0.0;
    for (int k = 0; k < FFT_N;) {
      if (!hotb[k]) { k++; continue; }
      int lo = k;
      double pw = 0.0;
      while (k < FFT_N && hotb[k]) { pw += P[k]; k++; }
      int width = (k - 1) - lo + 1;
      double cb = 0.5 * (lo + (k - 1));
      if (cb > FFT_N / 2) cb -= FFT_N;
      double cfreq = g_center_hz + cb * binhz;
      if (width >= MIN_CLUSTER && width <= MAX_CLUSTER && pw > best_pwr &&
          cfreq >= g_f1 && cfreq <= g_f2) {
        best_pwr = pw; best_lo = lo; best_hi = k - 1;
      }
    }

    int have = (best_lo >= 0);
    double off = 0.0;
    if (have) {
      double sw = 0.0, swk = 0.0;
      for (int k = best_lo; k <= best_hi; k++) {
        int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
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
      /* fallback: check integrated energy around burst_center even if no hot cluster */
      if (!near) {
        int cbin = (int)(burst_center / binhz + 0.5);
        if (cbin < 0) cbin += FFT_N;
        int bw_bins = 20;
        double sig = 0.0, fl = 0.0;
        for (int dk = -bw_bins; dk <= bw_bins; dk++) {
          int k = (cbin + dk + FFT_N) % FFT_N;
          sig += P[k]; fl += floor_bin[k];
        }
        if (fl > 0.0 && sig > fl * 2.0) near = 1;
      }
      if (near) {
        below = 0;
        bcenter_sum += off; bcenter_n++;
        burst_center = bcenter_sum / bcenter_n;
        double fl = 0.0;
        for (int k = best_lo; k <= best_hi; k++) fl += floor_bin[k];
        if (fl > 0.0) {
          double s = 10.0 * log10(best_pwr / fl);
          if (s > bsnr) bsnr = s;
        }
      } else {
        below++;
      }
      uint64_t cur_len = rd - burst_start;
      if (below >= OFF_FRAMES || cur_len > max_burst) {
        uint64_t bend = rd - (uint64_t)below * FFT_N;
        uint64_t blen = bend - burst_start;
        double fmeas, bwmeas;
        if (blen >= min_burst && blen <= max_burst &&
            measure_burst(burst_start, blen, &fmeas, &bwmeas)) {
          const char *type = (bwmeas > BW_SPLIT_HZ) ? "SGB" : "FGB";
          double dur_s = (double)blen / samp_rate;
          int is_sgb = (bwmeas > BW_SPLIT_HZ);
          int dur_ok = is_sgb ? (dur_s >= SGB_DUR_MIN && dur_s <= SGB_DUR_MAX)
                              : (dur_s >= FGB_DUR_MIN && dur_s <= FGB_DUR_MAX);
          static uint64_t prev_burst_start = 0;
          double dt = (prev_burst_start && burst_start >= prev_burst_start)
                          ? (double)(burst_start - prev_burst_start) / samp_rate
                          : 0.0;
          prev_burst_start = burst_start;
          if (!dur_ok) {
            DIAG("[%s] REJECT %.4f MHz   BW ~%3.0f kHz   %s   "
                 "dur %.2f s (out of range)\n",
                 timestr_ms(), (g_center_hz + fmeas) / 1e6, bwmeas / 1e3,
                 type, dur_s);
          } else {
            printf("[%s] BURST  %.4f MHz   BW ~%3.0f kHz   %s   "
                   "SNR %2.0f dB   dur %.2f s   dt %.3f s\n",
                   timestr_ms(), (g_center_hz + fmeas) / 1e6, bwmeas / 1e3,
                   type, bsnr, dur_s, dt);
            fflush(stdout);
            if (is_sgb)
              decode_sgb(burst_start, blen, fmeas, bsnr);
            else
              decode_fgb(burst_start, blen, fmeas, bsnr);
          }
        }
        state = 0; above = 0;
        for (int k = 0; k < FFT_N; k++)
          if (hotb[k]) floor_bin[k] = P[k];
      }
    }

    time_t now = time(NULL);
    if (now - last_beat >= HEARTBEAT_S) {
      last_beat = now;
      double fl = 0.0;
      for (int k = 0; k < FFT_N; k++) fl += floor_bin[k];
      DIAG("[%s] monitoring — mean noise floor %.2e, overruns %lu\n",
           timestr(now), fl / FFT_N, overruns);
    }
  }
  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: %s <freq_start> <freq_end> [sensitivity_gain] [bias_tee]\n"
            "  e.g. %s 1544.05M 1544.15M\n"
            "       %s 406.0M 406.1M 15\n"
            "       %s 1544.05M 1544.15M 15 1\n"
            "\n"
            "  sensitivity_gain: 0..21 (default: 15)\n"
            "  bias_tee: 0 or 1 (default: 0)\n",
            argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }

  double f1 = parse_freq(argv[1]);
  double f2 = parse_freq(argv[2]);
  if (f2 < f1) { double t = f1; f1 = f2; f2 = t; }
  int gain = (argc > 3) ? atoi(argv[3]) : 15;
  int bias_tee = (argc > 4) ? atoi(argv[4]) : 0;

  if (gain < 0) gain = 0;
  if (gain > 21) gain = 21;

  g_center_hz = (f1 + f2) / 2.0;
  g_f1 = f1;
  g_f2 = f2;
  double span = f2 - f1;

  printf("dec406_scan_airspy — unified FGB+SGB real-time decoder (Airspy Mini)\n");
  printf("  band    : %.3f - %.3f MHz   (span %.0f kHz)\n",
         f1 / 1e6, f2 / 1e6, span / 1e3);
  printf("  center  : %.3f MHz\n", g_center_hz / 1e6);
  fflush(stdout);

  /* Open Airspy */
  int rc = airspy_open(&adev);
  if (rc != AIRSPY_SUCCESS) {
    fprintf(stderr, "ERROR: airspy_open failed: %s\n", airspy_error_name(rc));
    return 1;
  }

  /* Query available sample rates and pick the lowest >= 2.4 MSPS */
  uint32_t n_rates = 0;
  airspy_get_samplerates(adev, &n_rates, 0);
  uint32_t *rates = malloc(n_rates * sizeof(uint32_t));
  airspy_get_samplerates(adev, rates, n_rates);

  printf("  available rates:");
  for (uint32_t i = 0; i < n_rates; i++) printf(" %.1f", rates[i] / 1e6);
  printf(" MSPS\n");

  samp_rate = rates[0];
  for (uint32_t i = 0; i < n_rates; i++) {
    if (rates[i] >= 2400000 && rates[i] < samp_rate)
      samp_rate = rates[i];
  }
  free(rates);

  printf("  selected: %.4f MSPS   (SPS=%.1f chips/sample for SGB)\n",
         samp_rate / 1e6, (double)samp_rate / 38400.0);

  if (span > samp_rate)
    DWARN("WARNING: band span %.0f Hz exceeds sample rate %u Hz\n", span, samp_rate);

  /* Configure */
  airspy_set_samplerate(adev, samp_rate);
  airspy_set_freq(adev, (uint32_t)g_center_hz);
  airspy_set_sample_type(adev, AIRSPY_SAMPLE_FLOAT32_IQ);
  airspy_set_sensitivity_gain(adev, (uint8_t)gain);
  airspy_set_rf_bias(adev, (uint8_t)(bias_tee ? 1 : 0));

  printf("  gain    : sensitivity=%d, bias_tee=%s\n",
         gain, bias_tee ? "ON" : "OFF");
  printf("  ring    : %.0f MB   (~%.1f s)\n",
         (double)RING_SAMPLES * sizeof(float complex) / 1e6,
         (double)RING_SAMPLES / samp_rate);

  int alerts_ok = (scan_alert_load_config("data/config_mail.txt") == 0);
  printf("  alerts  : %s\n", alerts_ok ? "enabled" : "disabled");
  if (alerts_ok) scan_alert_print_config_summary();
  printf("\n");
  fflush(stdout);

  /* Init */
  for (int k = 0; k < FFT_N; k++)
    hann[k] = 0.5 - 0.5 * cos(2.0 * M_PI * k / (FFT_N - 1));

  ring = malloc((size_t)RING_SAMPLES * sizeof(float complex));
  if (!ring) {
    fprintf(stderr, "ERROR: ring allocation failed\n");
    airspy_close(adev);
    return 1;
  }
  memset(ring, 0, (size_t)RING_SAMPLES * sizeof(float complex));

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  /* Start streaming */
  g_wr = 0;
  overruns = 0;
  running = 1;

  rc = airspy_start_rx(adev, airspy_rx_callback, NULL);
  if (rc != AIRSPY_SUCCESS) {
    fprintf(stderr, "ERROR: airspy_start_rx failed: %s\n", airspy_error_name(rc));
    free(ring);
    airspy_close(adev);
    return 1;
  }

  /* Processing runs in main thread context (callback is in a lib thread) */
  pthread_t proc_t;
  pthread_create(&proc_t, NULL, process_thread, NULL);

  /* Wait until stopped */
  while (running && airspy_is_streaming(adev) == AIRSPY_TRUE)
    usleep(100000);

  running = 0;
  pthread_mutex_lock(&lock);
  pthread_cond_broadcast(&data_avail);
  pthread_mutex_unlock(&lock);
  pthread_join(proc_t, NULL);

  airspy_stop_rx(adev);
  airspy_close(adev);
  free(ring);

  printf("\nstopped — %lu ring overrun(s)\n", overruns);
  return 0;
}
