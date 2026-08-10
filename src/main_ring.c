/**
 * @file main_ring.c
 * @brief dec406_ring — triggered IQ recorder for the 1544 MHz downlink.
 *
 * SDR-agnostic: uses the same backend abstraction and auto-detection as
 * dec406_scan (RTL-SDR, Airspy, PlutoSDR, HackRF). The backend fills the
 * scanner ring at full rate; this recorder consumes that ring, decimates to
 * ~500 kSPS, writes a disk ring of short chunks (10 min sliding window), runs
 * the same spectral FGB/SGB detector, and on a detection promotes the chunks
 * covering [burst - margin, burst + margin] into a self-contained SigMF file
 * under keep/. No decoding here — deferred to dec406_iq / dec406_fgb_iq.
 *
 * Usage: dec406_ring <freq_start> <freq_end> [gain] [extra] [ringdir]
 *   e.g. dec406_ring 1544.4M 1544.6M 21 0 /data/ring
 *   Force a backend with DEC406_BACKEND=airspy|rtlsdr|pluto|hackrf.
 */

#define _GNU_SOURCE
#include "backend.h"
#include "diag_log.h"
#include "scanner.h"
#include <complex.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── recording geometry ── */
#define REC_RATE_TARGET 500000
#define CHUNK_SEC       10
#define N_CHUNKS        60           /* 10 min sliding window */
#define MARGIN_SEC      10

/* ── detection (mirrors dec406_scan, retuned for 500 kSPS / FFT 2048) ── */
#define FFT_N 2048
#define DC_GUARD_BINS 3
#define NF_ALPHA 0.02
#define DET_FACTOR 7.0
#define SMOOTH_W 64
#define SMOOTH_FACTOR 1.4
#define MIN_CLUSTER 2
#define MAX_CLUSTER 400
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

static volatile sig_atomic_t running = 1;
static void on_sigint(int s) { (void)s; running = 0; }

static int decim = 5;
static double rec_rate = 500000.0;
static double g_center_hz = 0.0, g_f1 = 0.0, g_f2 = 0.0;
static double hann[FFT_N];
static double floor_bin[FFT_N];
static char ringdir[512] = "ring";
static long chunk_num_of[N_CHUNKS];
static const long SPC = (long)REC_RATE_TARGET * CHUNK_SEC;   /* samples/chunk */

static double parse_freq(const char *s) {
  char *end; double v = strtod(s, &end);
  switch (*end) { case 'k': case 'K': v *= 1e3; break;
                  case 'M': v *= 1e6; break; case 'G': v *= 1e9; break; }
  return v;
}
static const char *timestr(time_t t) {
  static char b[16]; struct tm tm; localtime_r(&t, &tm);
  strftime(b, sizeof b, "%H:%M:%S", &tm); return b;
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

/* one decimated sample at decimated index `d`, boxcar over the full-rate ring */
static inline float complex deci_sample(scanner_t *s, uint64_t d) {
  float complex acc = 0.0f;
  uint64_t base = d * (uint64_t)decim;
  for (int k = 0; k < decim; k++)
    acc += s->ring[(base + (uint64_t)k) & SCANNER_RING_MASK];
  return acc / (float)decim;
}

/* refined freq/bandwidth of a completed burst (decimated indices) */
static int measure_burst(scanner_t *s, uint64_t start, uint64_t len,
                         double *freq_off, double *bw_hz) {
  static double spec[FFT_N];
  static float complex win[FFT_N];
  if ((start + len) * (uint64_t)decim + SCANNER_RING_SAMPLES <= s->wr) return 0;

  for (int k = 0; k < FFT_N; k++) spec[k] = 0.0;
  int navg = (int)(len / FFT_N);
  if (navg < 1) navg = 1;
  if (navg > BURST_AVG) navg = BURST_AVG;
  uint64_t span = (len > (uint64_t)FFT_N) ? len - FFT_N : 0;
  uint64_t step = (navg > 1) ? span / (uint64_t)(navg - 1) : 0;
  for (int a = 0; a < navg; a++) {
    uint64_t base = start + (uint64_t)a * step;
    for (int i = 0; i < FFT_N; i++)
      win[i] = deci_sample(s, base + (uint64_t)i) * (float)hann[i];
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
  const double binhz = rec_rate / FFT_N;
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
    sum_p += spec[k]; sum_fp += f * spec[k];
    if (f < min_off) min_off = f;
    if (f > max_off) max_off = f;
  }
  if (sum_p <= 0.0) return 0;
  *freq_off = sum_fp / sum_p;
  *bw_hz = max_off - min_off;
  if (*bw_hz > BURST_BW_MAX) return 0;
  return 1;
}

static void iso8601(uint64_t abs_sample, uint64_t now_dec, char *out, size_t n) {
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  double back = (double)(now_dec - abs_sample) / rec_rate;
  time_t t = ts.tv_sec - (time_t)back;
  struct tm tm; gmtime_r(&t, &tm);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void promote(uint64_t a, uint64_t b, uint64_t burst_start, uint64_t burst_len,
                    double freq_hz, const char *label, uint64_t now_dec, FILE *cur_fp) {
  if (cur_fp) fflush(cur_fp);
  long first = (long)(a / (uint64_t)SPC);
  long last  = (long)((b - 1) / (uint64_t)SPC);

  char base[640], dpath[700], mpath[700];
  time_t nowt = time(NULL); struct tm tm; localtime_r(&nowt, &tm);
  char stamp[32]; strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &tm);
  snprintf(base, sizeof base, "%s/../keep/burst_%s_%.0fkHz", ringdir, stamp, freq_hz / 1e3);
  snprintf(dpath, sizeof dpath, "%s.sigmf-data", base);
  snprintf(mpath, sizeof mpath, "%s.sigmf-meta", base);

  FILE *out = fopen(dpath, "wb");
  if (!out) { DWARN("promote: cannot open %s\n", dpath); return; }
  uint64_t written_start = (uint64_t)first * (uint64_t)SPC;
  static int16_t buf[65536 * 2];
  for (long cn = first; cn <= last; cn++) {
    int slot = (int)((cn % N_CHUNKS + N_CHUNKS) % N_CHUNKS);
    if (chunk_num_of[slot] != cn) {
      DWARN("promote: chunk %ld overwritten/absent\n", cn);
      if (cn == first) written_start = (uint64_t)(cn + 1) * (uint64_t)SPC;
      continue;
    }
    char cpath[700];
    snprintf(cpath, sizeof cpath, "%s/chunk_%02d.iq", ringdir, slot);
    FILE *in = fopen(cpath, "rb");
    if (!in) { DWARN("promote: cannot read %s\n", cpath); continue; }
    size_t r;
    while ((r = fread(buf, sizeof(int16_t) * 2, 65536, in)) > 0)
      fwrite(buf, sizeof(int16_t) * 2, r, out);
    fclose(in);
  }
  fclose(out);

  char dt[40]; iso8601(written_start, now_dec, dt, sizeof dt);
  long ann_start = (long)(burst_start - written_start);
  if (ann_start < 0) ann_start = 0;
  FILE *m = fopen(mpath, "w");
  if (m) {
    fprintf(m,
      "{\n"
      "  \"global\": {\n"
      "    \"core:datatype\": \"ci16_le\",\n"
      "    \"core:sample_rate\": %.0f,\n"
      "    \"core:version\": \"1.0.0\",\n"
      "    \"core:description\": \"1544 MHz downlink, triggered ring capture\"\n"
      "  },\n"
      "  \"captures\": [{ \"core:sample_start\": 0, "
      "\"core:frequency\": %.0f, \"core:datetime\": \"%s\" }],\n"
      "  \"annotations\": [{ \"core:sample_start\": %ld, \"core:sample_count\": %llu,\n"
      "    \"core:freq_lower_edge\": %.0f, \"core:freq_upper_edge\": %.0f,\n"
      "    \"core:label\": \"%s\" }]\n"
      "}\n",
      rec_rate, g_center_hz, dt, ann_start, (unsigned long long)burst_len,
      g_center_hz + freq_hz - 30000.0, g_center_hz + freq_hz + 30000.0, label);
    fclose(m);
  }
  DIAG("[%s] KEEP %s  (%s, %.4f MHz, %.2f s window)\n",
       timestr(nowt), base, label, freq_hz / 1e6,
       (double)(b - written_start) / rec_rate);
}

/* ── consume the scanner ring, decimate, record chunks, detect, promote ── */
static void process_ring(scanner_t *s) {
  static float complex win[FFT_N];
  static double P[FFT_N];
  static unsigned char hotb[FFT_N];
  static int16_t iobuf[FFT_N * 2];

  const double binhz = rec_rate / FFT_N;
  const long chunk_blocks = SPC / FFT_N;
  const uint64_t need_full = (uint64_t)FFT_N * (uint64_t)decim;
  uint64_t rd_full = 0, pos_dec = 0, frame = 0;
  int state = 0, above = 0, below = 0;
  double cand_off = 0.0, burst_center = 0.0;
  uint64_t cand_start = 0, burst_start = 0;
  double bcenter_sum = 0.0, bsnr = 0.0;
  int bcenter_n = 0;
  time_t last_beat = time(NULL);

  uint64_t min_burst = (uint64_t)(0.20 * rec_rate);
  uint64_t max_burst = (uint64_t)(1.50 * rec_rate);
  uint64_t margin = (uint64_t)(MARGIN_SEC * rec_rate);

  int pend_active = 0;
  uint64_t pend_a = 0, pend_b = 0, pend_bs = 0, pend_bl = 0;
  double pend_freq = 0.0; char pend_label[8] = "";

  long block_in_chunk = 0, cur_chunk_num = 0;
  int cur_slot = 0;
  char cpath[700];
  chunk_num_of[0] = 0;
  snprintf(cpath, sizeof cpath, "%s/chunk_%02d.iq", ringdir, 0);
  FILE *chunk_fp = fopen(cpath, "wb");
  if (!chunk_fp) { DWARN("cannot open %s\n", cpath); return; }

  while (running && s->running) {
    uint64_t wr = scanner_wait_samples(s, rd_full, need_full);
    if (wr - rd_full < need_full) break;
    if (wr - rd_full > SCANNER_RING_SAMPLES) {
      s->overruns++;
      DWARN("ring overrun (%lu)\n", s->overruns);
      /* realign to a decim boundary */
      rd_full = wr - SCANNER_RING_SAMPLES;
      rd_full -= rd_full % (uint64_t)decim;
      pos_dec = rd_full / (uint64_t)decim;
      state = 0; above = 0;
    }

    uint64_t fstart = pos_dec;
    for (int i = 0; i < FFT_N; i++) win[i] = deci_sample(s, pos_dec + (uint64_t)i);

    /* write decimated block to current disk chunk (int16 interleaved) */
    for (int i = 0; i < FFT_N; i++) {
      float re = crealf(win[i]) * 32768.0f, im = cimagf(win[i]) * 32768.0f;
      if (re > 32767.f) re = 32767.f; else if (re < -32768.f) re = -32768.f;
      if (im > 32767.f) im = 32767.f; else if (im < -32768.f) im = -32768.f;
      iobuf[2 * i] = (int16_t)lrintf(re);
      iobuf[2 * i + 1] = (int16_t)lrintf(im);
    }
    fwrite(iobuf, sizeof(int16_t) * 2, FFT_N, chunk_fp);
    if (++block_in_chunk >= chunk_blocks) {
      fclose(chunk_fp);
      cur_chunk_num++;
      cur_slot = (int)(cur_chunk_num % N_CHUNKS);
      chunk_num_of[cur_slot] = cur_chunk_num;
      snprintf(cpath, sizeof cpath, "%s/chunk_%02d.iq", ringdir, cur_slot);
      chunk_fp = fopen(cpath, "wb");
      if (!chunk_fp) { DWARN("cannot open %s\n", cpath); break; }
      block_in_chunk = 0;
    }

    rd_full += need_full;
    pos_dec += FFT_N;

    /* windowed FFT for detection */
    for (int i = 0; i < FFT_N; i++) win[i] *= (float)hann[i];
    fft(win, FFT_N);
    for (int k = 0; k < FFT_N; k++) {
      double re = crealf(win[k]), im = cimagf(win[k]);
      P[k] = re * re + im * im;
    }
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

    int best_lo = -1, best_hi = -1; double best_pwr = 0.0;
    for (int k = 0; k < FFT_N;) {
      if (!hotb[k]) { k++; continue; }
      int lo = k; double pw = 0.0;
      while (k < FFT_N && hotb[k]) { pw += P[k]; k++; }
      int width = (k - 1) - lo + 1;
      double cb = 0.5 * (lo + (k - 1));
      if (cb > FFT_N / 2) cb -= FFT_N;
      double cfreq = g_center_hz + cb * binhz;
      if (width >= MIN_CLUSTER && width <= MAX_CLUSTER && pw > best_pwr &&
          cfreq >= g_f1 && cfreq <= g_f2) { best_pwr = pw; best_lo = lo; best_hi = k - 1; }
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
      } else above = 0;
    }

    if (state == 1) {
      int near = have && fabs(off - burst_center) < CENTER_TOL * binhz;
      if (near) {
        below = 0; bcenter_sum += off; bcenter_n++;
        burst_center = bcenter_sum / bcenter_n;
        double fl = 0.0;
        for (int k = best_lo; k <= best_hi; k++) fl += floor_bin[k];
        if (fl > 0.0) { double sdb = 10.0 * log10(best_pwr / fl); if (sdb > bsnr) bsnr = sdb; }
      } else below++;

      uint64_t cur_len = pos_dec - burst_start;
      if (below >= OFF_FRAMES || cur_len > max_burst) {
        uint64_t bend = pos_dec - (uint64_t)below * FFT_N;
        uint64_t blen = bend - burst_start;
        double fmeas, bwmeas;
        if (blen >= min_burst && blen <= max_burst &&
            measure_burst(s, burst_start, blen, &fmeas, &bwmeas)) {
          int is_sgb = (bwmeas > BW_SPLIT_HZ);
          const char *type = is_sgb ? "SGB" : "FGB";
          double dur_s = (double)blen / rec_rate;
          int dur_ok = is_sgb ? (dur_s >= SGB_DUR_MIN && dur_s <= SGB_DUR_MAX)
                              : (dur_s >= FGB_DUR_MIN && dur_s <= FGB_DUR_MAX);
          if (dur_ok && !pend_active) {
            pend_a = (burst_start > margin) ? burst_start - margin : 0;
            pend_b = bend + margin;
            pend_bs = burst_start; pend_bl = blen;
            pend_freq = fmeas; snprintf(pend_label, sizeof pend_label, "%s", type);
            pend_active = 1;
            DIAG("[%s] DETECT %.4f MHz  BW ~%3.0f kHz  %s  dur %.2f s  -> pending keep\n",
                 timestr(time(NULL)), (g_center_hz + fmeas) / 1e6, bwmeas / 1e3, type, dur_s);
          }
        }
        state = 0; above = 0;
        for (int k = 0; k < FFT_N; k++) if (hotb[k]) floor_bin[k] = P[k];
      }
    }

    if (pend_active && pos_dec >= pend_b) {
      promote(pend_a, pend_b, pend_bs, pend_bl, pend_freq, pend_label, pos_dec, chunk_fp);
      pend_active = 0;
    }

    time_t now = time(NULL);
    if (now - last_beat >= HEARTBEAT_S) {
      last_beat = now;
      double fl = 0.0;
      for (int k = 0; k < FFT_N; k++) fl += floor_bin[k];
      DIAG("[%s] recording — noise floor %.2e, overruns %lu\n",
           timestr(now), fl / FFT_N, s->overruns);
    }
  }
  if (chunk_fp) fclose(chunk_fp);
}

/* backend table (same order as dec406_scan) */
static const backend_ops_t *backends[] = {
#ifdef HAVE_AIRSPY
  &backend_airspy,
#endif
#ifdef HAVE_RTLSDR
  &backend_rtlsdr,
#endif
#ifdef HAVE_PLUTO
  &backend_pluto,
#endif
#ifdef HAVE_HACKRF
  &backend_hackrf,
#endif
};
const backend_ops_t *backend_find_by_name(const char *name) {
  for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
    if (name && strcmp(backends[i]->name, name) == 0) return backends[i];
  return NULL;
}

int main(int argc, char **argv) {
    /* Line-buffer stdout: it is block-buffered on a pipe while stderr never
     * is, which reorders output under `2>&1 | tee`. */
    setvbuf(stdout, NULL, _IOLBF, 0);
  if (argc < 3) {
    fprintf(stderr,
      "Usage: %s <freq_start> <freq_end> [gain] [extra] [ringdir]\n"
      "  e.g. %s 1544.4M 1544.6M 21 0 /data/ring\n"
      "  Force SDR with DEC406_BACKEND=airspy|rtlsdr|pluto|hackrf\n",
      argv[0], argv[0]);
    return 1;
  }
  double f1 = parse_freq(argv[1]), f2 = parse_freq(argv[2]);
  if (f2 < f1) { double t = f1; f1 = f2; f2 = t; }
  int gain = (argc > 3) ? atoi(argv[3]) : 15;
  int extra = (argc > 4) ? atoi(argv[4]) : 0;
  if (argc > 5) snprintf(ringdir, sizeof ringdir, "%s", argv[5]);

  g_center_hz = (f1 + f2) / 2.0; g_f1 = f1; g_f2 = f2;
  mkdir(ringdir, 0755);
  char keepdir[560]; snprintf(keepdir, sizeof keepdir, "%s/../keep", ringdir);
  mkdir(keepdir, 0755);

  const backend_ops_t *ops = NULL;
  const char *forced = getenv("DEC406_BACKEND");
  if (forced) {
    ops = backend_find_by_name(forced);
    if (!ops) { fprintf(stderr, "ERROR: unknown backend '%s'\n", forced); return 1; }
  } else {
    for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
      if (backends[i]->probe()) { ops = backends[i]; break; }
  }
  if (!ops) { fprintf(stderr, "ERROR: no SDR backend available\n"); return 1; }

  uint32_t samp_rate = 0;
  backend_t *backend = ops->open((uint32_t)g_center_hz, &samp_rate, gain, extra);
  if (!backend) { fprintf(stderr, "ERROR: %s open failed\n", ops->name); return 1; }

  decim = (int)lrint((double)samp_rate / REC_RATE_TARGET);
  if (decim < 1) decim = 1;
  rec_rate = (double)samp_rate / decim;

  printf("dec406_ring — triggered downlink recorder\n");
  printf("  backend : %s%s%s\n", ops->name,
         ops->model ? " / " : "", ops->model ? ops->model(backend) : "");
  printf("  band    : %.3f - %.3f MHz   center %.3f MHz\n",
         f1 / 1e6, f2 / 1e6, g_center_hz / 1e6);
  printf("  raw rate: %.4f MSPS -> decim %d -> rec %.1f kSPS\n",
         samp_rate / 1e6, decim, rec_rate / 1e3);
  printf("  ring    : %s  (%d x %d s = %d min, ~%.1f GB)\n",
         ringdir, N_CHUNKS, CHUNK_SEC, N_CHUNKS * CHUNK_SEC / 60,
         (double)N_CHUNKS * SPC * 4.0 / 1e9);
  printf("  keep    : %s   margin %d s\n\n", keepdir, MARGIN_SEC);
  fflush(stdout);

  for (int k = 0; k < FFT_N; k++)
    hann[k] = 0.5 - 0.5 * cos(2.0 * M_PI * k / (FFT_N - 1));
  for (int i = 0; i < N_CHUNKS; i++) chunk_num_of[i] = -1;

  scanner_t scanner;
  if (scanner_init(&scanner, samp_rate, g_center_hz, f1, f2, ops->dc_guard_bins) < 0) {
    fprintf(stderr, "ERROR: scanner_init failed\n");
    ops->close(backend); return 1;
  }

  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

  running = 1;
  if (ops->start(backend, &scanner) != 0) {
    fprintf(stderr, "ERROR: %s start failed\n", ops->name);
    scanner_free(&scanner); ops->close(backend); return 1;
  }

  process_ring(&scanner);

  running = 0;
  scanner_stop(&scanner);
  ops->stop(backend);
  scanner_free(&scanner);
  ops->close(backend);
  printf("\nstopped — %lu overrun(s)\n", scanner.overruns);
  return 0;
}
