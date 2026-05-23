/**
 * @file main_scan.c
 * @brief dec406_scan — real-time FGB+SGB band scanner.
 *
 * A capture thread pipes raw uint8 I/Q from rtl_sdr into a circular
 * buffer; a processing thread runs a spectral burst detector (continuous
 * FFT, per-bin noise floor) that locates and classifies each burst
 * FGB (narrow) vs SGB (wide DSSS), and decodes both — SGB through the
 * DSSS chain, FGB through an IQ-domain biphase-L demodulator.
 *
 * Usage: dec406_scan <freq_start> <freq_end> [ppm] [gain_dB]
 *   e.g. dec406_scan 406.0M 406.1M
 *        dec406_scan 431.0M 432.0M 0 30
 */

#define _GNU_SOURCE
#include "audio_capture.h"
#include "dec406.h"
#include "dsss_demod.h"
#include <complex.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define SAMP_RATE 2457600u
#define RING_BITS 23
#define RING_SAMPLES (1u << RING_BITS) /* ~3.4 s at 2.4576 Msps */
#define RING_MASK (RING_SAMPLES - 1u)
#define RING_BYTES (RING_SAMPLES * 2u)
#define CAP_CHUNK_BYTES (1u << 17) /* 128 KB capture reads */

#define FFT_N 8192       /* spectral detection FFT (300 Hz/bin) */
#define DC_GUARD_BINS 10 /* mask the RTL DC spike */
#define NF_ALPHA 0.02    /* per-bin noise-floor IIR (cold bins) */
#define DET_FACTOR 7.0   /* raw per-bin hot test (catches the FGB carrier) */
#define SMOOTH_W 128      /* smoothing window (bins) for the wideband test */
#define SMOOTH_FACTOR 1.4 /* smoothed-band hot test (catches the DSSS SGB) */
#define MIN_CLUSTER 2    /* min contiguous hot bins for a beacon */
#define MAX_CLUSTER 600  /* max plausible beacon width (~180 kHz) */
#define CENTER_TOL 40    /* bins: cluster-centre stability window */
#define ON_FRAMES 4      /* frames a cluster persists -> burst start */
#define OFF_FRAMES 16    /* frames a cluster absent  -> burst end */
#define WARMUP_FRAMES 64 /* frames to settle the per-bin noise floor */
#define BURST_AVG 24     /* spectra averaged to measure a finished burst */
#define MIN_BURST_SAMP ((uint64_t)(0.20 * SAMP_RATE))
#define MAX_BURST_SAMP ((uint64_t)(1.50 * SAMP_RATE))
#define BW_SPLIT_HZ 50000.0 /* FGB / SGB split (-10 dB bandwidth) */
#define BURST_BW_MAX 150000.0 /* reject bursts wider than any real beacon */
#define HEARTBEAT_S 15
#define FGB_DECIM 128                    /* IQ decimation, FGB path */
#define FGB_RATE (SAMP_RATE / FGB_DECIM) /* 19200 Hz audio rate */

static volatile sig_atomic_t running = 1;
static void on_sigint(int s) {
  (void)s;
  running = 0;
}

static uint8_t *ring = NULL;
static uint64_t g_wr = 0; /* total samples written */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_avail = PTHREAD_COND_INITIALIZER;
static FILE *iq_pipe = NULL;
static unsigned long overruns = 0;
static double g_center_hz = 0.0;
static double g_f1 = 0.0, g_f2 = 0.0; /* requested band edges (Hz) */
static double hann[FFT_N];
static double floor_bin[FFT_N]; /* per-bin noise floor */

/* parse "406M", "406.1M", "431.5M" or a plain Hz value */
static double parse_freq(const char *s) {
  char *end;
  double v = strtod(s, &end);
  switch (*end) {
  case 'k':
  case 'K':
    v *= 1e3;
    break;
  case 'M':
    v *= 1e6;
    break;
  case 'G':
    v *= 1e9;
    break;
  default:
    break;
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

/* RTL-SDR specific: a device left claimed by a previous unclean exit
 * refuses to reopen (libusb error -6). Reset it before launching rtl_sdr,
 * the same workaround scan406.pl uses. Not needed once the receiver moves
 * to the Airspy Mini. */
static void reset_rtl_usb(void) {
  FILE *p = popen("lsusb", "r");
  if (!p)
    return;
  char line[256];
  int done = 0;
  while (fgets(line, sizeof line, p)) {
    unsigned bus, dev, vid, pid;
    if (sscanf(line, "Bus %u Device %u: ID %x:%x", &bus, &dev, &vid, &pid) ==
            4 &&
        vid == 0x0bda && (pid == 0x2832 || pid == 0x2838)) {
      char path[64];
      snprintf(path, sizeof path, "/dev/bus/usb/%03u/%03u", bus, dev);
      int fd = open(path, O_WRONLY);
      if (fd >= 0) {
        if (ioctl(fd, USBDEVFS_RESET, 0) == 0) {
          printf("  reset RTL-SDR USB device (%s)\n", path);
          done = 1;
        }
        close(fd);
      }
    }
  }
  pclose(p);
  if (done) {
    printf("  waiting for device re-enumeration...\n");
    fflush(stdout);
    usleep(2000000);
  }
}

/* in-place iterative radix-2 FFT, n a power of two */
static void fft(float complex *x, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      float complex t = x[i];
      x[i] = x[j];
      x[j] = t;
    }
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

/* Extract an SGB burst window from the ring, mix it down to baseband by the
 * frequency offset measured in analyze_burst, and run the DSSS chain on it.
 * The chain is a baseband receiver — it expects the SGB near 0 Hz; the burst
 * sits at an arbitrary offset within the wideband capture, hence the mix.
 * The capture runs at 2.4576 Msps — the DSSS chain's native rate
 * (64 samples/chip) — so the window feeds it directly. */
static void decode_sgb(uint64_t start, uint64_t len, double offset_hz) {
  uint64_t head = (uint64_t)(0.20 * SAMP_RATE); /* slack before onset */
  uint64_t tail = (uint64_t)(0.20 * SAMP_RATE); /* slack after burst */
  if (head > start)
    head = start;
  uint64_t ext_start = start - head;
  uint64_t ext_len = head + len + tail;

  pthread_mutex_lock(&lock);
  uint64_t wr = g_wr;
  pthread_mutex_unlock(&lock);
  if (ext_start + RING_SAMPLES <= wr) {
    fprintf(stderr, "WARNING: SGB window overwritten before decode\n");
    return;
  }
  if (ext_start + ext_len > wr)
    ext_len = wr - ext_start;

  float complex *win = malloc((size_t)ext_len * sizeof(float complex));
  if (!win) {
    fprintf(stderr, "WARNING: SGB window alloc failed\n");
    return;
  }
  double w = 2.0 * M_PI * offset_hz / (double)SAMP_RATE; /* +offset -> 0 Hz */
  for (uint64_t i = 0; i < ext_len; i++) {
    size_t off = (size_t)((ext_start + i) & RING_MASK) * 2;
    double si = ((double)ring[off] - 127.5) / 127.5;
    double sq = ((double)ring[off + 1] - 127.5) / 127.5;
    double ph = w * (double)i;
    double c = cos(ph), s = sin(ph);
    win[i] = (float)(si * c + sq * s) + (float)(sq * c - si * s) * I;
  }

  uint8_t bits[DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS];
  memset(bits, 0, sizeof bits);
  float z = 0.0f;
  float fs = (float)SAMP_RATE;
  int rc =
      dsss_receive_burst(win, (size_t)ext_len, fs / 38400.0f, fs, 0, bits, &z);
  free(win);

  if (rc == 0) {
    printf("  --- SGB frame decoded (z=%.1f) ---\n", z);
    decode_beacon(bits, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
  } else {
    printf("  SGB burst — decode failed (z=%.1f)\n", z);
  }
  fflush(stdout);
}

/* Extract an FGB burst, mix it to baseband, decimate and FM-demodulate it
 * to an audio-rate stream, then run F4EHY's biphase-L decoder on that
 * buffer. Pure IQ in — no WAV, no sox, no rtl_fm. */
static void decode_fgb(uint64_t start, uint64_t len, double offset_hz) {
  uint64_t head = (uint64_t)(0.05 * SAMP_RATE);
  uint64_t tail = (uint64_t)(0.10 * SAMP_RATE);
  if (head > start)
    head = start;
  uint64_t ext_start = start - head;
  uint64_t ext_len = head + len + tail;

  pthread_mutex_lock(&lock);
  uint64_t wr = g_wr;
  pthread_mutex_unlock(&lock);
  if (ext_start + RING_SAMPLES <= wr) {
    fprintf(stderr, "WARNING: FGB window overwritten before decode\n");
    return;
  }
  if (ext_start + ext_len > wr)
    ext_len = wr - ext_start;

  uint64_t ndec = ext_len / FGB_DECIM;
  if (ndec < 64)
    return;

  float complex *dec = malloc((size_t)ndec * sizeof(float complex));
  int *aud = malloc((size_t)ndec * sizeof(int));
  if (!dec || !aud) {
    free(dec);
    free(aud);
    return;
  }

  /* down-convert to baseband and boxcar-decimate the IQ */
  double w = 2.0 * M_PI * offset_hz / (double)SAMP_RATE;
  for (uint64_t b = 0; b < ndec; b++) {
    double ar = 0.0, ai = 0.0;
    for (int j = 0; j < FGB_DECIM; j++) {
      uint64_t k = b * FGB_DECIM + (uint64_t)j;
      size_t off = (size_t)((ext_start + k) & RING_MASK) * 2;
      double si = ((double)ring[off] - 127.5) / 127.5;
      double sq = ((double)ring[off + 1] - 127.5) / 127.5;
      double ph = w * (double)k;
      double c = cos(ph), s = sin(ph);
      ar += si * c + sq * s;
      ai += sq * c - si * s;
    }
    dec[b] = (float)(ar / FGB_DECIM) + (float)(ai / FGB_DECIM) * I;
  }

  /* FM-demodulate: instantaneous frequency = arg(x[n] * conj(x[n-1])) */
  aud[0] = 0;
  for (uint64_t b = 1; b < ndec; b++) {
    float complex d = dec[b] * conjf(dec[b - 1]);
    double f = atan2((double)cimagf(d), (double)crealf(d));
    aud[b] = (int)(f * 8000.0);
  }

  int r = capture_trame_buffer(aud, (int)ndec, FGB_RATE);
  free(dec);
  free(aud);
  if (r <= 0) {
    printf("  FGB burst — no frame decoded\n");
    fflush(stdout);
  }
}

static void *capture_thread(void *arg) {
  (void)arg;
  uint64_t wr = 0;
  while (running) {
    size_t off = (size_t)(wr & RING_MASK) * 2;
    size_t to_end = RING_BYTES - off;
    size_t want = to_end < CAP_CHUNK_BYTES ? to_end : CAP_CHUNK_BYTES;
    size_t n = fread(ring + off, 1, want, iq_pipe);
    if (n == 0) {
      running = 0;
      break;
    } /* EOF or rtl_sdr stopped */
    size_t ns = n / 2;
    wr += ns;
    pthread_mutex_lock(&lock);
    g_wr = wr;
    pthread_cond_signal(&data_avail);
    pthread_mutex_unlock(&lock);
  }
  pthread_mutex_lock(&lock);
  pthread_cond_broadcast(&data_avail);
  pthread_mutex_unlock(&lock);
  return NULL;
}

/* Measure a finished burst's centre frequency and -10 dB bandwidth from
 * spectra averaged across the whole burst. A single FFT frame of a DSSS
 * SGB gives a noisy spectrum whose centroid wanders ~10 kHz; averaging
 * BURST_AVG frames yields a stable estimate — needed because the SGB
 * chain's coarse acquisition is sensitive to the residual offset. */
static int measure_burst(uint64_t start, uint64_t len, double *freq_off,
                         double *bw_hz) {
  static double spec[FFT_N];
  static float complex win[FFT_N];

  pthread_mutex_lock(&lock);
  uint64_t wr = g_wr;
  pthread_mutex_unlock(&lock);
  if (start + RING_SAMPLES <= wr)
    return 0; /* window overwritten before measurement */

  for (int k = 0; k < FFT_N; k++)
    spec[k] = 0.0;

  int navg = (int)(len / FFT_N);
  if (navg < 1)
    navg = 1;
  if (navg > BURST_AVG)
    navg = BURST_AVG;
  uint64_t span = (len > (uint64_t)FFT_N) ? len - FFT_N : 0;
  uint64_t step = (navg > 1) ? span / (uint64_t)(navg - 1) : 0;

  for (int a = 0; a < navg; a++) {
    uint64_t base = start + (uint64_t)a * step;
    for (int i = 0; i < FFT_N; i++) {
      size_t off = (size_t)((base + (uint64_t)i) & RING_MASK) * 2;
      double si = ((double)ring[off] - 127.5) * hann[i];
      double sq = ((double)ring[off + 1] - 127.5) * hann[i];
      win[i] = (float)si + (float)sq * I;
    }
    fft(win, FFT_N);
    for (int k = 0; k < FFT_N; k++) {
      double re = crealf(win[k]), im = cimagf(win[k]);
      spec[k] += re * re + im * im;
    }
  }

  const double binhz = (double)SAMP_RATE / FFT_N;
  double peak = 0.0;
  for (int k = 0; k < FFT_N; k++) {
    int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
    double fa = g_center_hz + ki * binhz;
    if (abs(ki) <= DC_GUARD_BINS || fa < g_f1 || fa > g_f2)
      continue; /* restrict to the requested band */
    if (spec[k] > peak)
      peak = spec[k];
  }
  if (peak <= 0.0)
    return 0;

  double thr = peak * 0.1; /* -10 dB */
  double sum_p = 0.0, sum_fp = 0.0, min_off = 1e12, max_off = -1e12;
  for (int k = 0; k < FFT_N; k++) {
    int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
    double fa = g_center_hz + ki * binhz;
    if (abs(ki) <= DC_GUARD_BINS || fa < g_f1 || fa > g_f2 || spec[k] < thr)
      continue;
    double f = ki * binhz;
    sum_p += spec[k];
    sum_fp += f * spec[k];
    if (f < min_off)
      min_off = f;
    if (f > max_off)
      max_off = f;
  }
  if (sum_p <= 0.0)
    return 0;

  *freq_off = sum_fp / sum_p;
  *bw_hz = max_off - min_off;
  if (*bw_hz > BURST_BW_MAX)
    return 0; /* wider than any beacon — interference or overload */
  return 1;
}

/* Spectral burst detector. Each FFT frame, a per-bin noise floor is kept;
 * a beacon shows up as a cluster of contiguous bins above their own floor,
 * which a wideband-power detector cannot see when the band is mostly noise.
 * A cluster that persists ON_FRAMES, with a plausible width, is a burst;
 * its centre gives the frequency, its width the FGB/SGB classification. */
static void *process_thread(void *arg) {
  (void)arg;
  static float complex win[FFT_N];
  static double P[FFT_N];
  static unsigned char hotb[FFT_N];

  const double binhz = (double)SAMP_RATE / FFT_N;
  uint64_t rd = 0, frame = 0;
  int state = 0; /* 0 = idle, 1 = in burst */
  int above = 0, below = 0;
  double cand_off = 0.0, burst_center = 0.0;
  uint64_t cand_start = 0, burst_start = 0;
  double bcenter_sum = 0.0, bsnr = 0.0;
  int bcenter_n = 0;
  time_t last_beat = time(NULL);

  while (running) {
    pthread_mutex_lock(&lock);
    while (running && (g_wr - rd) < FFT_N)
      pthread_cond_wait(&data_avail, &lock);
    uint64_t avail = g_wr - rd;
    pthread_mutex_unlock(&lock);
    if (avail < FFT_N)
      break;

    if (avail > RING_SAMPLES) { /* capture lapped the reader */
      overruns++;
      fprintf(stderr, "WARNING: ring overrun (%lu)\n", overruns);
      rd = g_wr - RING_SAMPLES;
      state = 0;
      above = 0;
    }

    uint64_t fstart = rd;
    for (int i = 0; i < FFT_N; i++) {
      size_t off = (size_t)((fstart + (uint64_t)i) & RING_MASK) * 2;
      double si = ((double)ring[off] - 127.5) * hann[i];
      double sq = ((double)ring[off + 1] - 127.5) * hann[i];
      win[i] = (float)si + (float)sq * I;
    }
    fft(win, FFT_N);
    for (int k = 0; k < FFT_N; k++) {
      double re = crealf(win[k]), im = cimagf(win[k]);
      P[k] = re * re + im * im;
    }
    rd += FFT_N;
    frame++;
    if (frame == 1)
      for (int k = 0; k < FFT_N; k++)
        floor_bin[k] = P[k];

    /* Hot-bin test. The FGB is a narrowband carrier (high power density):
     * the raw per-bin threshold catches it. The SGB is DSSS — its power is
     * spread over ~77 kHz at low density and never crosses the raw
     * threshold — so it is also tested on a band-integrated (smoothed)
     * spectrum, where the spread power stands clear of the noise. A bin is
     * hot if either test fires. Prefix sums give the smoothing in O(N). */
    static double pref[FFT_N + 1], preff[FFT_N + 1];
    pref[0] = preff[0] = 0.0;
    for (int k = 0; k < FFT_N; k++) {
      pref[k + 1] = pref[k] + P[k];
      preff[k + 1] = preff[k] + floor_bin[k];
    }
    for (int k = 0; k < FFT_N; k++) {
      int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
      if (abs(ki) <= DC_GUARD_BINS) {
        hotb[k] = 0;
        continue;
      }
      int raw_hot = P[k] > floor_bin[k] * DET_FACTOR;
      int lo = k - SMOOTH_W / 2, hi = k + SMOOTH_W / 2;
      if (lo < 0)
        lo = 0;
      if (hi > FFT_N - 1)
        hi = FFT_N - 1;
      int smooth_hot = (pref[hi + 1] - pref[lo]) >
                       (preff[hi + 1] - preff[lo]) * SMOOTH_FACTOR;
      hotb[k] = (raw_hot || smooth_hot) ? 1 : 0;
    }
    /* noise-floor IIR on cold bins only — a beacon never lifts its floor */
    for (int k = 0; k < FFT_N; k++)
      if (!hotb[k])
        floor_bin[k] += NF_ALPHA * (P[k] - floor_bin[k]);

    /* strongest plausible cluster of contiguous hot bins */
    int best_lo = -1, best_hi = -1;
    double best_pwr = 0.0;
    for (int k = 0; k < FFT_N;) {
      if (!hotb[k]) {
        k++;
        continue;
      }
      int lo = k;
      double pw = 0.0;
      while (k < FFT_N && hotb[k]) {
        pw += P[k];
        k++;
      }
      int width = (k - 1) - lo + 1;
      /* keep only clusters whose centre falls in the requested band —
       * the capture is 2.4576 MHz wide (SGB rate) but the decoder must
       * ignore everything outside [f1, f2] so out-of-band emitters never
       * occupy the single-burst detector */
      double cb = 0.5 * (lo + (k - 1));
      if (cb > FFT_N / 2)
        cb -= FFT_N;
      double cfreq = g_center_hz + cb * binhz;
      if (width >= MIN_CLUSTER && width <= MAX_CLUSTER && pw > best_pwr &&
          cfreq >= g_f1 && cfreq <= g_f2) {
        best_pwr = pw;
        best_lo = lo;
        best_hi = k - 1;
      }
    }

    int have = (best_lo >= 0);
    double off = 0.0;
    if (have) {
      /* power-weighted centroid of the cluster — stable against the
       * edge bins flickering frame to frame, unlike the midpoint */
      double sw = 0.0, swk = 0.0;
      for (int k = best_lo; k <= best_hi; k++) {
        int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
        sw += P[k];
        swk += (double)ki * P[k];
      }
      if (sw > 0.0)
        off = swk / sw * binhz;
    }

    if (state == 0 && frame > WARMUP_FRAMES) {
      if (have) {
        if (!(above > 0 && fabs(off - cand_off) < CENTER_TOL * binhz)) {
          above = 0;
          cand_start = fstart;
        }
        above++;
        cand_off = off;
        if (above >= ON_FRAMES) {
          state = 1;
          burst_start = cand_start;
          burst_center = cand_off;
          below = 0;
          bcenter_sum = 0.0;
          bcenter_n = 0;
          bsnr = 0.0;
        }
      } else {
        above = 0;
      }
    }

    if (state == 1) {
      /* a cluster only continues THIS burst if it sits at the burst's
       * frequency — a cluster elsewhere is another signal or noise and
       * must not keep the burst alive (which would run it to the MAX
       * timeout and pollute the averaged centre and the decode window) */
      int near = have && fabs(off - burst_center) < CENTER_TOL * binhz;
      if (near) {
        below = 0;
        bcenter_sum += off;
        bcenter_n++;
        burst_center = bcenter_sum / bcenter_n;
        double fl = 0.0;
        for (int k = best_lo; k <= best_hi; k++)
          fl += floor_bin[k];
        if (fl > 0.0) {
          double s = 10.0 * log10(best_pwr / fl);
          if (s > bsnr)
            bsnr = s;
        }
      } else {
        below++;
      }
      uint64_t cur_len = rd - burst_start;
      if (below >= OFF_FRAMES || cur_len > MAX_BURST_SAMP) {
        uint64_t bend = rd - (uint64_t)below * FFT_N;
        uint64_t len = bend - burst_start;
        double fmeas, bwmeas;
        if (len >= MIN_BURST_SAMP && len <= MAX_BURST_SAMP &&
            measure_burst(burst_start, len, &fmeas, &bwmeas)) {
          const char *type = (bwmeas > BW_SPLIT_HZ) ? "SGB" : "FGB";
          printf("[%s] BURST  %.4f MHz   BW ~%3.0f kHz   %s   "
                 "SNR %2.0f dB   dur %.2f s\n",
                 timestr(time(NULL)), (g_center_hz + fmeas) / 1e6, bwmeas / 1e3,
                 type, bsnr, (double)len / SAMP_RATE);
          fflush(stdout);
          if (bwmeas > BW_SPLIT_HZ)
            decode_sgb(burst_start, len, fmeas);
          else
            decode_fgb(burst_start, len, fmeas);
        }
        state = 0;
        above = 0;
      }
    }

    time_t now = time(NULL);
    if (now - last_beat >= HEARTBEAT_S) {
      last_beat = now;
      double fl = 0.0;
      for (int k = 0; k < FFT_N; k++)
        fl += floor_bin[k];
      printf("[%s] monitoring — mean noise floor %.0f, overruns %lu\n",
             timestr(now), fl / FFT_N, overruns);
      fflush(stdout);
    }
  }
  return NULL;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: %s <freq_start> <freq_end> [ppm] [gain_dB]\n"
            "  e.g. %s 406.0M 406.1M\n"
            "       %s 431.0M 432.0M 0 30\n",
            argv[0], argv[0], argv[0]);
    return 1;
  }

  double f1 = parse_freq(argv[1]);
  double f2 = parse_freq(argv[2]);
  if (f2 < f1) {
    double t = f1;
    f1 = f2;
    f2 = t;
  }
  int ppm = (argc > 3) ? atoi(argv[3]) : 0;
  int has_gain = (argc > 4);
  int gain = has_gain ? atoi(argv[4]) : 0;

  double span = f2 - f1;
  g_center_hz = (f1 + f2) / 2.0;
  g_f1 = f1;
  g_f2 = f2;

  if (span > SAMP_RATE)
    fprintf(stderr,
            "WARNING: band span %.0f Hz exceeds sample rate %u Hz "
            "— part of the band will not be captured\n",
            span, SAMP_RATE);

  char cmd[256];
  if (has_gain)
    snprintf(cmd, sizeof cmd, "rtl_sdr -f %.0f -s %u -p %d -g %d -",
             g_center_hz, SAMP_RATE, ppm, gain);
  else
    snprintf(cmd, sizeof cmd, "rtl_sdr -f %.0f -s %u -p %d -", g_center_hz,
             SAMP_RATE, ppm);

  printf("dec406_scan — unified FGB+SGB real-time decoder\n");
  printf("  band    : %.3f - %.3f MHz   (span %.0f kHz)\n", f1 / 1e6, f2 / 1e6,
         span / 1e3);
  printf("  center  : %.3f MHz   sample rate %.4f Msps\n", g_center_hz / 1e6,
         SAMP_RATE / 1e6);
  printf("  ring    : %.0f MB   (~%.1f s)\n", RING_BYTES / 1e6,
         (double)RING_SAMPLES / SAMP_RATE);
  printf("  rtl_sdr : %s\n\n", cmd);
  fflush(stdout);

  for (int k = 0; k < FFT_N; k++)
    hann[k] = 0.5 - 0.5 * cos(2.0 * M_PI * k / (FFT_N - 1));

  ring = malloc(RING_BYTES);
  if (!ring) {
    fprintf(stderr, "ERROR: ring allocation failed\n");
    return 1;
  }

  reset_rtl_usb();

  iq_pipe = popen(cmd, "r");
  if (!iq_pipe) {
    fprintf(stderr, "ERROR: cannot start rtl_sdr (is it installed?)\n");
    free(ring);
    return 1;
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  pthread_t cap_t, proc_t;
  pthread_create(&cap_t, NULL, capture_thread, NULL);
  pthread_create(&proc_t, NULL, process_thread, NULL);
  pthread_join(cap_t, NULL);
  pthread_join(proc_t, NULL);

  pclose(iq_pipe);
  free(ring);
  printf("\nstopped — %lu ring overrun(s)\n", overruns);
  return 0;
}
