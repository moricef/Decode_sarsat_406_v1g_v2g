#define _GNU_SOURCE
#include "backend.h"
#include "diag_log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libairspy/airspy.h>

struct backend {
    struct airspy_device *dev;
    scanner_t *scanner;
    uint32_t samp_rate;
    const char *model_name;
    int running;
};

/* R2 and Mini report the same libairspy board_id; the reliable discriminator
 * is the maximum supported sample rate (R2: 10 MSPS, Mini: 6 MSPS). */
static const char *airspy_model_from_max_rate(uint32_t max_rate) {
    if (max_rate >= 10000000) return "Airspy R2";
    if (max_rate >= 6000000)  return "Airspy Mini";
    return "Airspy";
}

static int airspy_probe_dev(void) {
    struct airspy_device *dev = NULL;
    int rc = airspy_open(&dev);
    if (rc == AIRSPY_SUCCESS) {
        airspy_close(dev);
        return 1;
    }
    return 0;
}

static int airspy_rx_cb(airspy_transfer *transfer) {
    backend_t *b = (backend_t *)transfer->ctx;
    if (!b->running) return -1;

    float *src = (float *)transfer->samples;
    int count = transfer->sample_count;
    float complex tmp[count];
    for (int i = 0; i < count; i++)
        tmp[i] = src[2*i] + src[2*i + 1] * I;

    scanner_push(b->scanner, tmp, (size_t)count);
    return 0;
}

static backend_t *airspy_open_dev(uint32_t freq_hz, uint32_t *samp_rate,
                                   int gain, int bias_tee) {
    backend_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;

    int rc = airspy_open(&b->dev);
    if (rc != AIRSPY_SUCCESS) { free(b); return NULL; }

    uint32_t n_rates = 0;
    airspy_get_samplerates(b->dev, &n_rates, 0);
    uint32_t *rates = malloc(n_rates * sizeof(uint32_t));
    airspy_get_samplerates(b->dev, rates, n_rates);

    b->samp_rate = rates[0];
    uint32_t max_rate = rates[0];
    for (uint32_t i = 0; i < n_rates; i++) {
        if (rates[i] >= 2400000 && rates[i] < b->samp_rate)
            b->samp_rate = rates[i];
        if (rates[i] > max_rate)
            max_rate = rates[i];
    }
    free(rates);
    b->model_name = airspy_model_from_max_rate(max_rate);

    airspy_set_samplerate(b->dev, b->samp_rate);
    airspy_set_freq(b->dev, freq_hz);
    airspy_set_sample_type(b->dev, AIRSPY_SAMPLE_FLOAT32_IQ);
    if (gain < 0) gain = 15;
    if (gain > 21) gain = 21;
    airspy_set_sensitivity_gain(b->dev, (uint8_t)gain);
    airspy_set_rf_bias(b->dev, (uint8_t)(bias_tee ? 1 : 0));

    *samp_rate = b->samp_rate;
    return b;
}

static int airspy_start_dev(backend_t *b, scanner_t *s) {
    b->scanner = s;
    b->running = 1;
    return airspy_start_rx(b->dev, airspy_rx_cb, b);
}

static void airspy_stop_dev(backend_t *b) {
    b->running = 0;
    airspy_stop_rx(b->dev);
}

static void airspy_close_dev(backend_t *b) {
    airspy_close(b->dev);
    free(b);
}

static const char *airspy_model_dev(backend_t *b) {
    return b->model_name;
}

const backend_ops_t backend_airspy = {
    .name = "Airspy Mini",
    .probe = airspy_probe_dev,
    .open = airspy_open_dev,
    .start = airspy_start_dev,
    .stop = airspy_stop_dev,
    .close = airspy_close_dev,
    .model = airspy_model_dev,
    .dc_guard_bins = 4
};
