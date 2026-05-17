/**
 * @file main_scan.c
 * @brief dec406_scan — real-time FGB+SGB band scanner.
 *
 * Phase 1: ingestion. Pipes raw uint8 I/Q from rtl_sdr into a double
 * ping-pong buffer; a capture thread fills one buffer while a processing
 * thread consumes the other. Phase 1 processing only measures throughput
 * and mean power — burst detection and decoding come in later phases.
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

#define DEFAULT_SAMP_RATE  2400000u
#define BUF_SAMPLES        (1u << 21)            /* complex samples per buffer */
#define BUF_BYTES          (BUF_SAMPLES * 2u)    /* uint8 I,Q interleaved */
#define REPORT_EVERY       4                     /* buffers between stat lines */

static volatile sig_atomic_t running = 1;
static void on_sigint(int s) { (void)s; running = 0; }

typedef struct {
    uint8_t *data;
    size_t   fill;      /* bytes actually filled */
    int      ready;     /* 1 = filled, awaiting the processing thread */
} iqbuf_t;

static iqbuf_t         buf[2];
static pthread_mutex_t lock       = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  buf_free   = PTHREAD_COND_INITIALIZER;
static FILE           *iq_pipe    = NULL;
static unsigned long   stalls     = 0;   /* times capture waited on processing */

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

/* Capture and processing alternate over the two buffers in lockstep:
 * capture fills 0,1,0,1… and processing consumes them in the same order. */
static void *capture_thread(void *arg) {
    (void)arg;
    int idx = 0;
    while (running) {
        iqbuf_t *b = &buf[idx];
        size_t got = 0;
        while (got < BUF_BYTES && running) {
            size_t n = fread(b->data + got, 1, BUF_BYTES - got, iq_pipe);
            if (n == 0) { running = 0; break; }   /* EOF or rtl_sdr stopped */
            got += n;
        }
        if (!running) break;

        pthread_mutex_lock(&lock);
        b->fill  = got;
        b->ready = 1;
        pthread_cond_signal(&data_ready);
        int next = idx ^ 1;
        if (buf[next].ready) stalls++;            /* processing fell behind */
        while (buf[next].ready && running)
            pthread_cond_wait(&buf_free, &lock);
        pthread_mutex_unlock(&lock);
        idx = next;
    }
    pthread_mutex_lock(&lock);
    pthread_cond_broadcast(&data_ready);          /* release processing on exit */
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void *process_thread(void *arg) {
    (void)arg;
    int idx = 0;
    unsigned long total = 0, nbuf = 0;
    struct timespec t0;
    int timed = 0;   /* rate clock starts at the first buffer, not at thread start */

    while (running) {
        pthread_mutex_lock(&lock);
        while (running && !buf[idx].ready)
            pthread_cond_wait(&data_ready, &lock);
        int have = buf[idx].ready;
        pthread_mutex_unlock(&lock);
        if (!have) break;

        iqbuf_t *b = &buf[idx];
        size_t nsamp = b->fill / 2;
        double acc = 0.0;
        for (size_t i = 0; i < nsamp; i++) {
            double I = (double)b->data[2 * i]     - 127.5;
            double Q = (double)b->data[2 * i + 1] - 127.5;
            acc += I * I + Q * Q;
        }
        double meanpwr = nsamp ? acc / (double)nsamp : 0.0;
        nbuf++;
        if (!timed) { clock_gettime(CLOCK_MONOTONIC, &t0); timed = 1; }
        else        { total += nsamp; }

        pthread_mutex_lock(&lock);
        b->ready = 0;
        pthread_cond_signal(&buf_free);
        pthread_mutex_unlock(&lock);

        if (timed && nbuf % REPORT_EVERY == 0) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double dt = (t1.tv_sec - t0.tv_sec)
                      + (t1.tv_nsec - t0.tv_nsec) / 1e9;
            double msps = dt > 0 ? total / dt / 1e6 : 0.0;
            printf("[scan] %.3f Msamp/s   mean power %8.0f   stalls %lu\n",
                   msps, meanpwr, stalls);
            fflush(stdout);
        }
        idx ^= 1;
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

    double   center    = (f1 + f2) / 2.0;
    double   span      = f2 - f1;
    unsigned samp_rate = DEFAULT_SAMP_RATE;

    if (span > samp_rate)
        fprintf(stderr, "WARNING: band span %.0f Hz exceeds sample rate %u Hz "
                        "— part of the band will not be captured\n",
                span, samp_rate);

    char cmd[256];
    if (has_gain)
        snprintf(cmd, sizeof cmd,
                 "rtl_sdr -f %.0f -s %u -p %d -g %d -",
                 center, samp_rate, ppm, gain);
    else
        snprintf(cmd, sizeof cmd,
                 "rtl_sdr -f %.0f -s %u -p %d -",
                 center, samp_rate, ppm);

    printf("dec406_scan — Phase 1 (ingestion + double buffer)\n");
    printf("  band    : %.3f - %.3f MHz   (span %.0f kHz)\n",
           f1 / 1e6, f2 / 1e6, span / 1e3);
    printf("  center  : %.3f MHz   sample rate %.4f Msps\n",
           center / 1e6, samp_rate / 1e6);
    printf("  buffers : 2 x %.1f MB   (~%.2f s each)\n",
           BUF_BYTES / 1e6, (double)BUF_SAMPLES / samp_rate);
    printf("  rtl_sdr : %s\n\n", cmd);
    fflush(stdout);

    buf[0].data = malloc(BUF_BYTES);
    buf[1].data = malloc(BUF_BYTES);
    if (!buf[0].data || !buf[1].data) {
        fprintf(stderr, "ERROR: buffer allocation failed\n");
        return 1;
    }

    iq_pipe = popen(cmd, "r");
    if (!iq_pipe) {
        fprintf(stderr, "ERROR: cannot start rtl_sdr (is it installed?)\n");
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
    free(buf[0].data);
    free(buf[1].data);
    printf("\nstopped — %lu capture stall(s)\n", stalls);
    return 0;
}
