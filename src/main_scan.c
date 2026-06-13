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
#include "diag_log.h"
#include "dsss_demod.h"
#include "fgb_iq_demod.h"
#include "scan_alert.h"
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
#include <rtl-sdr.h>

#define SAMP_RATE 2457600u
#define RING_BITS 24
#define RING_SAMPLES (1u << RING_BITS) /* ~6.8 s at 2.4576 Msps */
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
#define BW_SPLIT_HZ 20000.0 /* FGB / SGB split (-10 dB bandwidth) */
#define BURST_BW_MAX 150000.0 /* reject bursts wider than any real beacon */
#define HEARTBEAT_S 15
#define FGB_DECIM 128                    /* IQ decimation, FGB path */
#define FGB_RATE (SAMP_RATE / FGB_DECIM) /* 19200 Hz audio rate */

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t sigint_recvd = 0;
static void on_sigint(int s) {
  (void)s;
  running = 0;
  sigint_recvd = 1;
}

static uint8_t *ring = NULL;
static uint64_t g_wr = 0; /* total samples written */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t data_avail = PTHREAD_COND_INITIALIZER;
static rtlsdr_dev_t *rtl_dev = NULL;
/* Samples per acquisition cycle. The historical 55 s restart was a
 * workaround for rtl_sdr's async-mode libusb state corruption; with
 * synchronous librtlsdr reads that failure mode is gone, so the cycle
 * only serves as a periodic dongle refresh. 10 min instead of 55 s
 * cuts the number of cycle boundaries (each one a window where an
 * SGB burst gets truncated → 'buffer too short') by a factor of 11. */
#define CYCLE_SAMPLES ((uint64_t)SAMP_RATE * 600u)
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

/* Wall-clock with millisecond resolution — to see burst grouping (rafales
 * spaced by tens of ms) that the second-resolution timestr() hides. */
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

/* RTL-SDR specific: a device left claimed by a previous unclean exit
 * refuses to reopen (libusb error -6). Reset it before launching rtl_sdr,
 * the same workaround scan406.pl uses. Not needed once the receiver moves
 * to the Airspy Mini. */

/* Silence librtlsdr's internal printf chatter ("Reattached kernel driver",
 * "[R82XX] PLL not locked!", etc.) around rtlsdr_open/configure/close. The
 * library writes those to stderr directly, bypassing our diag macros, so
 * the only way to hide them is to redirect fd 2 for the duration of the
 * call. Returns the saved fd (negative on error); pass it back to restore. */
static int silence_stderr_begin(void) {
  fflush(stderr);
  int saved = dup(STDERR_FILENO);
  int devnull = open("/dev/null", O_WRONLY);
  if (devnull >= 0) {
    dup2(devnull, STDERR_FILENO);
    close(devnull);
  }
  return saved;
}
static void silence_stderr_end(int saved) {
  if (saved < 0) return;
  fflush(stderr);
  dup2(saved, STDERR_FILENO);
  close(saved);
}

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
          DIAG("  reset RTL-SDR USB device (%s)\n", path);
          done = 1;
        }
        close(fd);
      }
    }
  }
  pclose(p);
  if (done) {
    DIAG("  waiting for device re-enumeration...\n");
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
/* Run a decode that prints to stdout, capturing its output to a heap
 * buffer. The buffer is also re-printed to the real stdout so the live
 * log keeps the human-readable trace. Returns a malloc'd string the
 * caller must free, or NULL on capture failure (in which case the decode
 * was still run normally — fallback path). */
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
    fputs(buf, stdout);  /* echo to real stdout */
  }
  fclose(tmp);
  return buf;
}

static void decode_sgb(uint64_t start, uint64_t len, double offset_hz, double snr_db) {
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
    DWARN("WARNING: SGB window overwritten before decode\n");
    return;
  }
  if (ext_start + ext_len > wr)
    ext_len = wr - ext_start;

  float complex *win = malloc((size_t)ext_len * sizeof(float complex));
  if (!win) {
    DWARN("WARNING: SGB window alloc failed\n");
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
    char *body = capture_decode(decode_beacon, bits,
                                DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    double freq_mhz = (g_center_hz + offset_hz) / 1e6;
    /* Only alert on a SGB frame that fully decoded AND is operational.
     * Positive match on "Test Protocol: Normal Operation" (T.018 §3
     * bit 43 = 0, dec406_v2g.c:775). Any other body — BCH-uncorrectable
     * ("FRAME REJECTED" with no Test Protocol line), test beacon
     * ("Test Protocol: Active (Non-operational)" — CNES on channel K,
     * TAC 65535), or any corruption — is silenced. */
    int is_real_distress =
        (body && strstr(body, "Test Protocol: Normal Operation") != NULL);
    /* Repetition gate (same as FGB): require a second sighting of the
     * same Hex ID within 3 min before mailing. */
    const char *hex_id = scan_alert_extract_hex_id(body);
    int is_repeat = scan_alert_is_repeat(hex_id);
    if (is_real_distress && is_repeat &&
        scan_alert_freq_authorised(freq_mhz)) {
      scan_alert_send("SGB", freq_mhz, snr_db, bits,
                      DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS, body);
    }
    free(body);
  } else {
    printf("  SGB burst — decode failed (z=%.1f)\n", z);
  }
  printf("\n");  /* blank line between frames — separates the firehose */
  fflush(stdout);
}

/* Extract an FGB burst, mix it to baseband at the FULL sample rate, and
 * run the IQ-direct demodulator/decoder. No FM-demod, no audio detour. */
static void decode_fgb(uint64_t start, uint64_t len, double offset_hz, double snr_db) {
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
    DWARN("WARNING: FGB window overwritten before decode\n");
    return;
  }
  if (ext_start + ext_len > wr)
    ext_len = wr - ext_start;

  float complex *win = malloc((size_t)ext_len * sizeof(float complex));
  if (!win) {
    DWARN("WARNING: FGB window alloc failed\n");
    return;
  }

  /* Down-convert to baseband at FULL rate (no decimation). */
  double w = 2.0 * M_PI * offset_hz / (double)SAMP_RATE;
  for (uint64_t i = 0; i < ext_len; i++) {
    size_t off = (size_t)((ext_start + i) & RING_MASK) * 2;
    double si = ((double)ring[off] - 127.5) / 127.5;
    double sq = ((double)ring[off + 1] - 127.5) / 127.5;
    double ph = w * (double)i;
    double c = cos(ph), s = sin(ph);
    win[i] = (float)(si * c + sq * s) + (float)(sq * c - si * s) * I;
  }

  uint8_t bits[FGB_LONG_BITS];
  int rc = fgb_iq_decode(win, (size_t)ext_len, SAMP_RATE, (long)head, bits);
  free(win);

  if (rc == 0) {
    char *body = capture_decode(decode_1g, bits, FGB_LONG_BITS);
    double freq_mhz = (g_center_hz + offset_hz) / 1e6;
    /* Filter A — orbitography beacons (Cospas-Sarsat ground reference
     * stations, not distress). decode_1g prints "Identification:
     * Orbitography" for them. They emit continuously, would otherwise
     * spam every cycle. */
    int is_orbitography =
        (body && strstr(body, "Identification: Orbitography") != NULL);
    /* Filter B — repetition gate. Real distress repeats every ~50 s;
     * one-shot bench tests fire one burst then stop. Require a second
     * sighting of the same Hex ID within 3 min before mailing. */
    const char *hex_id = scan_alert_extract_hex_id(body);
    int is_repeat = scan_alert_is_repeat(hex_id);
    /* Filter C — unprogrammed identification. decode_1g prints
     * "ID-NOT-AVAIL" when the beacon serial/identifier is all-zero,
     * which means a factory-fresh unit being bench-tested (e.g. the
     * Airbus ELT-DT bench at Toulouse Blagnac on channel D). A real
     * distress beacon has a configured identity. */
    int is_id_not_avail = (body && strstr(body, "ID-NOT-AVAIL") != NULL);
    if (body && !is_orbitography && !is_id_not_avail && is_repeat &&
        scan_alert_freq_authorised(freq_mhz)) {
      scan_alert_send("FGB", freq_mhz, snr_db, bits, FGB_LONG_BITS, body);
    }
    free(body);
  } else if (rc == -2) {
    printf("  FGB burst — CRC FAIL (bits sliced but CRC mismatched both polarities)\n");
  } else {
    printf("  FGB burst — no frame decoded (no sync/burst)\n");
  }
  printf("\n");  /* blank line between frames — separates the firehose */
  fflush(stdout);
}

static void *capture_thread(void *arg) {
  (void)arg;
  uint64_t wr = 0;
  while (running && wr < CYCLE_SAMPLES) {
    size_t off = (size_t)(wr & RING_MASK) * 2;
    size_t to_end = RING_BYTES - off;
    size_t want = to_end < CAP_CHUNK_BYTES ? to_end : CAP_CHUNK_BYTES;
    int n_read = 0;
    int rc = rtlsdr_read_sync(rtl_dev, ring + off, (int)want, &n_read);
    if (rc < 0 || n_read <= 0) {
      running = 0;
      break;
    }
    size_t ns = (size_t)n_read / 2;
    wr += ns;
    pthread_mutex_lock(&lock);
    g_wr = wr;
    pthread_cond_signal(&data_avail);
    pthread_mutex_unlock(&lock);
  }
  running = 0;  /* signal process_thread the cycle is done */
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
      DWARN("WARNING: ring overrun (%lu)\n", overruns);
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
          /* Sample-accurate gap since the previous burst onset — reveals
           * the tens-of-ms grouping that wall-clock print latency blurs. */
          static uint64_t prev_burst_start = 0;
          /* burst_start < prev means g_wr was reset on an rtl_sdr restart;
           * skip the cross-cycle gap (would underflow). */
          double dt = (prev_burst_start && burst_start >= prev_burst_start)
                          ? (double)(burst_start - prev_burst_start) / SAMP_RATE
                          : 0.0;
          prev_burst_start = burst_start;
          printf("[%s] BURST  %.4f MHz   BW ~%3.0f kHz   %s   "
                 "SNR %2.0f dB   dur %.2f s   dt %.3f s\n",
                 timestr_ms(), (g_center_hz + fmeas) / 1e6, bwmeas / 1e3,
                 type, bsnr, (double)len / SAMP_RATE, dt);
          fflush(stdout);
          if (bwmeas > BW_SPLIT_HZ)
            decode_sgb(burst_start, len, fmeas, bsnr);
          else
            decode_fgb(burst_start, len, fmeas, bsnr);
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
      DIAG("[%s] monitoring — mean noise floor %.0f, overruns %lu\n",
           timestr(now), fl / FFT_N, overruns);
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
    DWARN("WARNING: band span %.0f Hz exceeds sample rate %u Hz "
          "— part of the band will not be captured\n",
          span, SAMP_RATE);

  printf("dec406_scan — unified FGB+SGB real-time decoder\n");
  printf("  band    : %.3f - %.3f MHz   (span %.0f kHz)\n", f1 / 1e6, f2 / 1e6,
         span / 1e3);
  printf("  center  : %.3f MHz   sample rate %.4f Msps\n", g_center_hz / 1e6,
         SAMP_RATE / 1e6);
  printf("  ring    : %.0f MB   (~%.1f s)\n", RING_BYTES / 1e6,
         (double)RING_SAMPLES / SAMP_RATE);
  if (has_gain)
    printf("  rtl-sdr : librtlsdr sync, gain=%d dB, ppm=%d, cycle=%u s\n",
           gain, ppm, (unsigned)(CYCLE_SAMPLES / SAMP_RATE));
  else
    printf("  rtl-sdr : librtlsdr sync, gain=auto, ppm=%d, cycle=%u s\n",
           ppm, (unsigned)(CYCLE_SAMPLES / SAMP_RATE));

  /* Email alerts on authorised T.012 channels, ported from scan406.pl. */
  int alerts_ok = (scan_alert_load_config("data/config_mail.txt") == 0);
  printf("  alerts  : %s\n\n", alerts_ok ? "enabled" : "disabled");
  fflush(stdout);

  for (int k = 0; k < FFT_N; k++)
    hann[k] = 0.5 - 0.5 * cos(2.0 * M_PI * k / (FFT_N - 1));

  ring = malloc(RING_BYTES);
  if (!ring) {
    DERR("ERROR: ring allocation failed\n");
    return 1;
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  while (1) {
    reset_rtl_usb();

    /* Open + configure RTL-SDR in synchronous read mode. This replaces the
     * popen("rtl_sdr ...") pipe: no async libusb callbacks → no spurious
     * 'cb transfer status: 5' on bus hiccups. The dongle is closed and
     * reopened at each cycle (same as the historical pipe restart). */
    int saved_err = silence_stderr_begin();
    int rc = rtlsdr_open(&rtl_dev, 0);
    silence_stderr_end(saved_err);
    if (rc < 0) {
      DERR("ERROR: rtlsdr_open failed (%d), retrying after reset\n", rc);
      sleep(1);
      continue;
    }
    saved_err = silence_stderr_begin();
    rtlsdr_set_sample_rate(rtl_dev, SAMP_RATE);
    rtlsdr_set_center_freq(rtl_dev, (uint32_t)g_center_hz);
    rtlsdr_set_freq_correction(rtl_dev, ppm);
    if (has_gain) {
      rtlsdr_set_tuner_gain_mode(rtl_dev, 1);
      rtlsdr_set_tuner_gain(rtl_dev, gain * 10);  /* tenths of dB */
    } else {
      rtlsdr_set_tuner_gain_mode(rtl_dev, 0);     /* AGC */
    }
    rtlsdr_reset_buffer(rtl_dev);
    silence_stderr_end(saved_err);

    running = 1;
    g_wr = 0;
    overruns = 0;
    pthread_t cap_t, proc_t;
    pthread_create(&cap_t, NULL, capture_thread, NULL);
    pthread_create(&proc_t, NULL, process_thread, NULL);
    pthread_join(cap_t, NULL);
    pthread_join(proc_t, NULL);

    saved_err = silence_stderr_begin();
    rtlsdr_close(rtl_dev);
    silence_stderr_end(saved_err);
    rtl_dev = NULL;
    if (sigint_recvd)
      break;
  }
  free(ring);
  printf("\nstopped — %lu ring overrun(s)\n", overruns);
  return 0;
}
