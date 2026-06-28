#ifndef SCANNER_H
#define SCANNER_H

#include <complex.h>
#include <stdint.h>
#include <stddef.h>

#define SCANNER_RING_BITS 24
#define SCANNER_RING_SAMPLES (1u << SCANNER_RING_BITS)
#define SCANNER_RING_MASK (SCANNER_RING_SAMPLES - 1u)

#define SCANNER_FFT_N 8192

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
} scanner_t;

int scanner_init(scanner_t *s, uint32_t samp_rate, double center_hz,
                 double f1, double f2, int dc_guard_bins);
void scanner_free(scanner_t *s);

void scanner_push(scanner_t *s, const float complex *samples, size_t n);

void scanner_process(scanner_t *s);

void scanner_stop(scanner_t *s);

#endif
