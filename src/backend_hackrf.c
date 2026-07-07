#define _GNU_SOURCE
#include "backend.h"
#include "diag_log.h"
#include <libhackrf/hackrf.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HACKRF_SAMP_RATE 2457600u

struct backend {
    hackrf_device *dev;
    scanner_t *scanner;
    int running;
    int gain;
    int extra;
    float complex *tmp;
    size_t tmp_cap;
};

static int hackrf_probe_dev(void) {
    if (hackrf_init() != HACKRF_SUCCESS)
        return 0;
    hackrf_device *dev = NULL;
    int rc = hackrf_open(&dev);
    if (rc == HACKRF_SUCCESS && dev) {
        hackrf_close(dev);
        hackrf_exit();
        return 1;
    }
    hackrf_exit();
    return 0;
}

static uint32_t clamp_vga_gain(int gain) {
    if (gain < 0) gain = 24;
    if (gain > 62) gain = 62;
    if (gain & 1) gain--;
    if (gain < 0) gain = 0;
    return (uint32_t)gain;
}

static uint32_t clamp_lna_gain(int gain) {
    static const uint32_t steps[] = {0, 8, 16, 24, 32, 40};
    uint32_t best = steps[0];
    int target = (gain < 0) ? 16 : gain;
    int best_err = 1000;
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        int err = abs(target - (int)steps[i]);
        if (err < best_err) {
            best_err = err;
            best = steps[i];
        }
    }
    return best;
}

static int hackrf_rx_cb(hackrf_transfer *transfer) {
    backend_t *b = (backend_t *)transfer->rx_ctx;
    if (!transfer || !b || !b->running || !transfer->buffer || transfer->valid_length <= 1)
        return 1;

    size_t ns = (size_t)transfer->valid_length / 2;
    if (ns > b->tmp_cap) {
        float complex *tmp = realloc(b->tmp, ns * sizeof(float complex));
        if (!tmp) {
            b->running = 0;
            return 1;
        }
        b->tmp = tmp;
        b->tmp_cap = ns;
    }

    const int8_t *src = (const int8_t *)transfer->buffer;
    for (size_t i = 0; i < ns; i++) {
        float si = (float)src[2*i] / 128.0f;
        float sq = (float)src[2*i + 1] / 128.0f;
        b->tmp[i] = si + sq * I;
    }

    scanner_push(b->scanner, b->tmp, ns);
    return 0;
}

static backend_t *hackrf_open_dev(uint32_t freq_hz, uint32_t *samp_rate,
                                  int gain, int extra) {
    backend_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;

    b->gain = gain;
    b->extra = extra;

    if (hackrf_init() != HACKRF_SUCCESS) {
        free(b);
        return NULL;
    }

    int rc = hackrf_open(&b->dev);
    if (rc != HACKRF_SUCCESS || !b->dev) {
        hackrf_exit();
        free(b);
        return NULL;
    }

    rc = hackrf_set_sample_rate(b->dev, HACKRF_SAMP_RATE);
    if (rc != HACKRF_SUCCESS) {
        hackrf_close(b->dev);
        hackrf_exit();
        free(b);
        return NULL;
    }

    hackrf_set_freq(b->dev, freq_hz);
    hackrf_set_amp_enable(b->dev, (extra > 0) ? 1 : 0);
    hackrf_set_antenna_enable(b->dev, (extra > 0) ? 1 : 0);
    hackrf_set_lna_gain(b->dev, clamp_lna_gain(gain));
    hackrf_set_vga_gain(b->dev, clamp_vga_gain(gain));

    *samp_rate = HACKRF_SAMP_RATE;
    return b;
}

static int hackrf_start_dev(backend_t *b, scanner_t *s) {
    b->scanner = s;
    b->running = 1;
    return hackrf_start_rx(b->dev, hackrf_rx_cb, b);
}

static void hackrf_stop_dev(backend_t *b) {
    b->running = 0;
    hackrf_stop_rx(b->dev);
}

static void hackrf_close_dev(backend_t *b) {
    if (b->dev)
        hackrf_close(b->dev);
    hackrf_exit();
    free(b->tmp);
    free(b);
}

const backend_ops_t backend_hackrf = {
    .name = "HackRF One",
    .probe = hackrf_probe_dev,
    .open = hackrf_open_dev,
    .start = hackrf_start_dev,
    .stop = hackrf_stop_dev,
    .close = hackrf_close_dev,
    .dc_guard_bins = 4
};
