/**
 * @file main_scan.c
 * @brief dec406_scan — real-time FGB+SGB band scanner.
 *
 * A capture thread pipes raw uint8 I/Q from rtl_sdr into a circular
 * buffer; a processing thread runs a streaming burst detector, measures
 * each burst's frequency and bandwidth (FFT), classifies it FGB (narrow)
 * vs SGB (wide DSSS), and decodes SGB bursts through the DSSS chain.
 * FGB decoding comes in a later phase.
 *
 * Usage: dec406_scan <freq_start> <freq_end> [ppm] [gain_dB]
 *   e.g. dec406_scan 406.0M 406.1M
 *        dec406_scan 431.0M 432.0M 0 30
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <complex.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include "dsss_demod.h"
#include "dec406.h"

#define SAMP_RATE        2457600u
#define RING_BITS        23
#define RING_SAMPLES     (1u << RING_BITS)        /* ~3.4 s at 2.4576 Msps */
#define RING_MASK        (RING_SAMPLES - 1u)
#define RING_BYTES       (RING_SAMPLES * 2u)
#define CAP_CHUNK_BYTES  (1u << 17)               /* 128 KB capture reads */

#define BLOCK            4096u                    /* power-detection block */
#define FFT_N            8192                     /* spectral-analysis FFT */
#define N_AVG            24                       /* spectra averaged per burst */
#define DC_GUARD_BINS    10                       /* mask the RTL DC spike */

#define NOISE_ALPHA      0.02                     /* noise-floor IIR */
#define BURST_FACTOR     5.0                      /* burst = power > floor x5 */
#define ON_BLOCKS        3                        /* blocks hot -> burst start */
#define OFF_BLOCKS       16                       /* blocks cold -> burst end */
#define MIN_BURST_SAMP   ((uint64_t)(0.15 * SAMP_RATE))
#define MAX_BURST_SAMP   ((uint64_t)(1.40 * SAMP_RATE))
#define BW_SPLIT_HZ      50000.0                  /* FGB/SGB classification */
#define HEARTBEAT_S      15

static volatile sig_atomic_t running = 1;
static void on_sigint(int s) { (void)s; running = 0; }

static uint8_t        *ring     = NULL;
static uint64_t        g_wr     = 0;      /* total samples written */
static pthread_mutex_t lock       = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  data_avail = PTHREAD_COND_INITIALIZER;
static FILE           *iq_pipe   = NULL;
static unsigned long   overruns  = 0;
static double          g_center_hz = 0.0;
static double          hann[FFT_N];

/* parse "406M", "406.1M", "431.5M" or a plain Hz value */
static double parse_freq(const char *s) {
    char *end;
    double v = strtod(s, &end);
    switch (*end) {
        case 'k': case 'K': v *= 1e3; break;
        case 'M':           v *= 1e6; break;
        case 'G':           v *= 1e9; break;
        default:                      break;
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
    if (!p) return;
    char line[256];
    int done = 0;
    while (fgets(line, sizeof line, p)) {
        unsigned bus, dev, vid, pid;
        if (sscanf(line, "Bus %u Device %u: ID %x:%x",
                   &bus, &dev, &vid, &pid) == 4
            && vid == 0x0bda && (pid == 0x2832 || pid == 0x2838)) {
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
                x[i + k]             = u + v;
                x[i + k + len / 2]   = u - v;
                w *= wl;
            }
        }
    }
}

/* mean |x|^2 over BLOCK samples starting at absolute index a */
static double block_power(uint64_t a) {
    double acc = 0.0;
    for (unsigned i = 0; i < BLOCK; i++) {
        size_t off = (size_t)((a + i) & RING_MASK) * 2;
        double si = (double)ring[off]     - 127.5;
        double sq = (double)ring[off + 1] - 127.5;
        acc += si * si + sq * sq;
    }
    return acc / BLOCK;
}

/* Extract an SGB burst window from the ring, mix it down to baseband by the
 * frequency offset measured in analyze_burst, and run the DSSS chain on it.
 * The chain is a baseband receiver — it expects the SGB near 0 Hz; the burst
 * sits at an arbitrary offset within the wideband capture, hence the mix.
 * The capture runs at 2.4576 Msps — the DSSS chain's native rate
 * (64 samples/chip) — so the window feeds it directly. */
static void decode_sgb(uint64_t start, uint64_t len, double offset_hz) {
    uint64_t head = (uint64_t)(0.05 * SAMP_RATE);   /* slack before onset */
    uint64_t tail = (uint64_t)(0.20 * SAMP_RATE);   /* slack after burst */
    if (head > start) head = start;
    uint64_t ext_start = start - head;
    uint64_t ext_len   = head + len + tail;

    pthread_mutex_lock(&lock);
    uint64_t wr = g_wr;
    pthread_mutex_unlock(&lock);
    if (ext_start + RING_SAMPLES <= wr) {
        fprintf(stderr, "WARNING: SGB window overwritten before decode\n");
        return;
    }
    if (ext_start + ext_len > wr) ext_len = wr - ext_start;

    float complex *win = malloc((size_t)ext_len * sizeof(float complex));
    if (!win) { fprintf(stderr, "WARNING: SGB window alloc failed\n"); return; }
    double w = 2.0 * M_PI * offset_hz / (double)SAMP_RATE;   /* +offset -> 0 Hz */
    for (uint64_t i = 0; i < ext_len; i++) {
        size_t off = (size_t)((ext_start + i) & RING_MASK) * 2;
        double si = ((double)ring[off]     - 127.5) / 127.5;
        double sq = ((double)ring[off + 1] - 127.5) / 127.5;
        double ph = w * (double)i;
        double c  = cos(ph), s = sin(ph);
        win[i] = (float)(si * c + sq * s) + (float)(sq * c - si * s) * I;
    }

    uint8_t bits[DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS];
    memset(bits, 0, sizeof bits);
    float z  = 0.0f;
    float fs = (float)SAMP_RATE;
    int rc = dsss_receive_burst(win, (size_t)ext_len, fs / 38400.0f,
                                fs, 0, bits, &z);
    free(win);

    if (rc == 0) {
        printf("  --- SGB frame decoded (z=%.1f) ---\n", z);
        decode_beacon(bits, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);
    } else {
        printf("  SGB burst — decode failed (z=%.1f)\n", z);
    }
    fflush(stdout);
}

/* FFT the burst window, measure centre frequency + bandwidth, classify */
static void analyze_burst(uint64_t start, uint64_t len,
                          double burst_pwr, double noise_pwr) {
    static double        spec[FFT_N];
    static float complex win[FFT_N];

    pthread_mutex_lock(&lock);
    uint64_t wr = g_wr;
    pthread_mutex_unlock(&lock);
    if (start + RING_SAMPLES <= wr) {
        fprintf(stderr, "WARNING: burst window overwritten before analysis\n");
        return;
    }

    for (int k = 0; k < FFT_N; k++) spec[k] = 0.0;

    int navg = (int)(len / FFT_N);
    if (navg < 1)     navg = 1;
    if (navg > N_AVG) navg = N_AVG;
    uint64_t span = (len > (uint64_t)FFT_N) ? len - FFT_N : 0;
    uint64_t step = (navg > 1) ? span / (uint64_t)(navg - 1) : 0;

    for (int w = 0; w < navg; w++) {
        uint64_t base = start + (uint64_t)w * step;
        for (int i = 0; i < FFT_N; i++) {
            size_t off = (size_t)((base + (uint64_t)i) & RING_MASK) * 2;
            double si = ((double)ring[off]     - 127.5) * hann[i];
            double sq = ((double)ring[off + 1] - 127.5) * hann[i];
            win[i] = (float)si + (float)sq * I;
        }
        fft(win, FFT_N);
        for (int k = 0; k < FFT_N; k++) {
            double re = crealf(win[k]), im = cimagf(win[k]);
            spec[k] += re * re + im * im;
        }
    }

    double binhz = (double)SAMP_RATE / FFT_N;
    double peak = 0.0;
    int peak_k = -1;
    for (int k = 0; k < FFT_N; k++) {
        int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
        if (abs(ki) <= DC_GUARD_BINS) continue;
        if (spec[k] > peak) { peak = spec[k]; peak_k = k; }
    }
    if (peak_k < 0) return;

    double thr = peak * 0.1;                 /* -10 dB */
    double sum_p = 0.0, sum_fp = 0.0;
    double min_off = 1e12, max_off = -1e12;
    int nbin = 0;
    for (int k = 0; k < FFT_N; k++) {
        int ki = (k <= FFT_N / 2) ? k : k - FFT_N;
        if (abs(ki) <= DC_GUARD_BINS) continue;
        if (spec[k] < thr) continue;
        double off = ki * binhz;
        sum_p  += spec[k];
        sum_fp += off * spec[k];
        if (off < min_off) min_off = off;
        if (off > max_off) max_off = off;
        nbin++;
    }
    if (nbin == 0 || sum_p <= 0.0) return;

    double centroid = sum_fp / sum_p;
    double bw       = max_off - min_off;
    double abs_hz   = g_center_hz + centroid;
    double snr      = 10.0 * log10(burst_pwr / (noise_pwr > 0 ? noise_pwr : 1.0));
    const char *type = (bw > BW_SPLIT_HZ) ? "SGB" : "FGB";
    double dur      = (double)len / SAMP_RATE;

    printf("[%s] BURST  %.4f MHz   BW ~%3.0f kHz   %s   SNR %2.0f dB   dur %.2f s\n",
           timestr(time(NULL)), abs_hz / 1e6, bw / 1e3, type, snr, dur);
    fflush(stdout);

    if (bw > BW_SPLIT_HZ)
        decode_sgb(start, len, centroid);
}

static void *capture_thread(void *arg) {
    (void)arg;
    uint64_t wr = 0;
    while (running) {
        size_t off     = (size_t)(wr & RING_MASK) * 2;
        size_t to_end  = RING_BYTES - off;
        size_t want    = to_end < CAP_CHUNK_BYTES ? to_end : CAP_CHUNK_BYTES;
        size_t n       = fread(ring + off, 1, want, iq_pipe);
        if (n == 0) { running = 0; break; }     /* EOF or rtl_sdr stopped */
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

static void *process_thread(void *arg) {
    (void)arg;
    uint64_t rd = 0;
    double   noise_pwr = -1.0;
    int      state = 0;            /* 0 = idle, 1 = in burst */
    int      above = 0, below = 0;
    uint64_t burst_start = 0;
    double   burst_pwr_sum = 0.0;
    uint64_t burst_blocks = 0;
    time_t   last_beat = time(NULL);

    while (running) {
        pthread_mutex_lock(&lock);
        while (running && (g_wr - rd) < BLOCK)
            pthread_cond_wait(&data_avail, &lock);
        uint64_t avail = g_wr - rd;
        pthread_mutex_unlock(&lock);
        if (avail < BLOCK) break;

        if (avail > RING_SAMPLES) {            /* capture lapped the reader */
            overruns++;
            fprintf(stderr, "WARNING: ring overrun (%lu)\n", overruns);
            rd = g_wr - RING_SAMPLES;
            state = 0; above = 0;
        }

        uint64_t blk = rd;
        double   pwr = block_power(blk);
        rd += BLOCK;
        if (noise_pwr < 0.0) noise_pwr = pwr;
        int hot = (pwr > noise_pwr * BURST_FACTOR);

        if (state == 0) {
            if (!hot) {
                noise_pwr += NOISE_ALPHA * (pwr - noise_pwr);
                above = 0;
            } else if (++above >= ON_BLOCKS) {
                state = 1;
                uint64_t back = (uint64_t)(ON_BLOCKS - 1) * BLOCK;
                burst_start = (blk > back) ? blk - back : 0;
                below = 0; burst_pwr_sum = 0.0; burst_blocks = 0;
            }
        } else {
            burst_pwr_sum += pwr;
            burst_blocks++;
            below = hot ? 0 : below + 1;
            uint64_t cur_len = (blk + BLOCK) - burst_start;
            if (below >= OFF_BLOCKS || cur_len > MAX_BURST_SAMP) {
                uint64_t end = (blk + BLOCK) - (uint64_t)below * BLOCK;
                uint64_t len = end - burst_start;
                if (len >= MIN_BURST_SAMP && len <= MAX_BURST_SAMP)
                    analyze_burst(burst_start, len,
                                  burst_pwr_sum / (burst_blocks ? burst_blocks : 1),
                                  noise_pwr);
                state = 0; above = 0;
            }
        }

        time_t now = time(NULL);
        if (now - last_beat >= HEARTBEAT_S) {
            last_beat = now;
            printf("[%s] monitoring — noise floor %.0f, overruns %lu\n",
                   timestr(now), noise_pwr, overruns);
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
    if (f2 < f1) { double t = f1; f1 = f2; f2 = t; }
    int ppm      = (argc > 3) ? atoi(argv[3]) : 0;
    int has_gain = (argc > 4);
    int gain     = has_gain ? atoi(argv[4]) : 0;

    double span = f2 - f1;
    g_center_hz = (f1 + f2) / 2.0;

    if (span > SAMP_RATE)
        fprintf(stderr, "WARNING: band span %.0f Hz exceeds sample rate %u Hz "
                        "— part of the band will not be captured\n",
                span, SAMP_RATE);

    char cmd[256];
    if (has_gain)
        snprintf(cmd, sizeof cmd, "rtl_sdr -f %.0f -s %u -p %d -g %d -",
                 g_center_hz, SAMP_RATE, ppm, gain);
    else
        snprintf(cmd, sizeof cmd, "rtl_sdr -f %.0f -s %u -p %d -",
                 g_center_hz, SAMP_RATE, ppm);

    printf("dec406_scan — Phase 2 (burst detection + classification)\n");
    printf("  band    : %.3f - %.3f MHz   (span %.0f kHz)\n",
           f1 / 1e6, f2 / 1e6, span / 1e3);
    printf("  center  : %.3f MHz   sample rate %.4f Msps\n",
           g_center_hz / 1e6, SAMP_RATE / 1e6);
    printf("  ring    : %.0f MB   (~%.1f s)\n",
           RING_BYTES / 1e6, (double)RING_SAMPLES / SAMP_RATE);
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
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pthread_t cap_t, proc_t;
    pthread_create(&cap_t,  NULL, capture_thread, NULL);
    pthread_create(&proc_t, NULL, process_thread, NULL);
    pthread_join(cap_t,  NULL);
    pthread_join(proc_t, NULL);

    pclose(iq_pipe);
    free(ring);
    printf("\nstopped — %lu ring overrun(s)\n", overruns);
    return 0;
}
