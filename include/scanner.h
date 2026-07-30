#ifndef SCANNER_H
#define SCANNER_H

#include <complex.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#define SCANNER_RING_BITS 24
#define SCANNER_RING_SAMPLES (1u << SCANNER_RING_BITS)
#define SCANNER_RING_MASK (SCANNER_RING_SAMPLES - 1u)

#define SCANNER_FFT_N 8192
#define SCANNER_DECODE_QUEUE_CAP 16

typedef enum {
    SCANNER_DECODE_FGB = 1,
    SCANNER_DECODE_SGB = 2
} scanner_decode_type_t;

typedef struct {
    scanner_decode_type_t type;
    float complex *samples;
    size_t n_samples;
    long head_samples;
    double offset_hz;
    double snr_db;
} scanner_decode_job_t;

typedef struct {
    float complex *ring;
    uint64_t wr;
    uint32_t samp_rate;
    double center_hz;
    double f1, f2;
    int dc_guard_bins;
    double hann[SCANNER_FFT_N];
    double floor_bin[SCANNER_FFT_N];
    unsigned long overruns;
    int running;
    pthread_t decode_thread;
    pthread_mutex_t decode_lock;
    pthread_cond_t decode_avail;
    scanner_decode_job_t decode_q[SCANNER_DECODE_QUEUE_CAP];
    unsigned decode_head;
    unsigned decode_tail;
    unsigned decode_count;
    unsigned long decode_drops;
    int decode_thread_started;
} scanner_t;

int scanner_init(scanner_t *s, uint32_t samp_rate, double center_hz,
                 double f1, double f2, int dc_guard_bins);
void scanner_free(scanner_t *s);

void scanner_push(scanner_t *s, const float complex *samples, size_t n);

void scanner_process(scanner_t *s);

void scanner_stop(scanner_t *s);

/* Block until at least `need` samples are available past `rd`, then return the
 * current write pointer. Lets an external consumer (e.g. the ring recorder)
 * read s->ring without duplicating the internal sync. Returns wr, which may be
 * < rd+need if the scanner was stopped. */
uint64_t scanner_wait_samples(scanner_t *s, uint64_t rd, uint64_t need);

#endif
