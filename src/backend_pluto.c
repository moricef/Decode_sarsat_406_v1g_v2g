#define _GNU_SOURCE
#include "backend.h"
#include "diag_log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iio.h>

#define PLUTO_SAMP_RATE 2457600u
#define PLUTO_BUF_SIZE (1u << 16)

struct backend {
    struct iio_context *ctx;
    struct iio_device *phy;
    struct iio_device *rx;
    struct iio_channel *rx0_i;
    struct iio_channel *rx0_q;
    struct iio_buffer *rxbuf;
    scanner_t *scanner;
    pthread_t cap_thread;
    int running;
};

static int pluto_probe_dev(void) {
    FILE *p = popen("lsusb 2>/dev/null | grep -q 'Analog Devices.*PlutoSDR\\|0456:b673' && echo found", "r");
    if (!p) return 0;
    char buf[16] = {0};
    fgets(buf, sizeof buf, p);
    pclose(p);
    if (strstr(buf, "found")) return 1;
    struct iio_context *ctx = iio_create_local_context();
    if (ctx) {
        struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
        iio_context_destroy(ctx);
        if (phy) return 1;
    }
    return 0;
}

static backend_t *pluto_open_dev(uint32_t freq_hz, uint32_t *samp_rate,
                                  int gain, int extra) {
    (void)extra;
    backend_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;

    b->ctx = iio_create_context_from_uri("ip:pluto.local");
    if (!b->ctx)
        b->ctx = iio_create_context_from_uri("ip:192.168.2.1");
    if (!b->ctx) { free(b); return NULL; }

    b->phy = iio_context_find_device(b->ctx, "ad9361-phy");
    b->rx = iio_context_find_device(b->ctx, "cf-ad9361-lpc");
    if (!b->phy || !b->rx) {
        iio_context_destroy(b->ctx);
        free(b);
        return NULL;
    }

    struct iio_channel *phy_rx = iio_device_find_channel(b->phy, "voltage0", false);
    if (!phy_rx) {
        iio_context_destroy(b->ctx);
        free(b);
        return NULL;
    }

    iio_channel_attr_write(phy_rx, "rf_port_select", "A_BALANCED");
    iio_channel_attr_write_longlong(phy_rx, "rf_bandwidth", PLUTO_SAMP_RATE);
    iio_channel_attr_write_longlong(phy_rx, "sampling_frequency", PLUTO_SAMP_RATE);

    struct iio_channel *lo = iio_device_find_channel(b->phy, "altvoltage0", true);
    if (lo)
        iio_channel_attr_write_longlong(lo, "frequency", freq_hz);

    if (gain < 0) {
        iio_channel_attr_write(phy_rx, "gain_control_mode", "slow_attack");
    } else {
        iio_channel_attr_write(phy_rx, "gain_control_mode", "manual");
        iio_channel_attr_write_longlong(phy_rx, "hardwaregain", gain);
    }

    b->rx0_i = iio_device_find_channel(b->rx, "voltage0", false);
    b->rx0_q = iio_device_find_channel(b->rx, "voltage1", false);
    if (!b->rx0_i || !b->rx0_q) {
        iio_context_destroy(b->ctx);
        free(b);
        return NULL;
    }
    iio_channel_enable(b->rx0_i);
    iio_channel_enable(b->rx0_q);

    b->rxbuf = iio_device_create_buffer(b->rx, PLUTO_BUF_SIZE, false);
    if (!b->rxbuf) {
        iio_context_destroy(b->ctx);
        free(b);
        return NULL;
    }

    *samp_rate = PLUTO_SAMP_RATE;
    return b;
}

static void *capture_loop(void *arg) {
    backend_t *b = (backend_t *)arg;

    while (b->running) {
        ssize_t nbytes = iio_buffer_refill(b->rxbuf);
        if (nbytes < 0) break;

        ptrdiff_t p_inc = iio_buffer_step(b->rxbuf);
        void *end = iio_buffer_end(b->rxbuf);
        void *p;

        size_t ns = (size_t)nbytes / (2 * sizeof(int16_t));
        float complex *tmp = malloc(ns * sizeof(float complex));
        if (!tmp) continue;

        size_t i = 0;
        for (p = iio_buffer_first(b->rxbuf, b->rx0_i); p < end; p += p_inc) {
            int16_t si = ((int16_t *)p)[0];
            int16_t sq = ((int16_t *)p)[1];
            tmp[i++] = (float)si / 2048.0f + (float)sq / 2048.0f * I;
        }

        scanner_push(b->scanner, tmp, i);
        free(tmp);
    }

    b->running = 0;
    scanner_stop(b->scanner);
    return NULL;
}

static int pluto_start_dev(backend_t *b, scanner_t *s) {
    b->scanner = s;
    b->running = 1;
    return pthread_create(&b->cap_thread, NULL, capture_loop, b);
}

static void pluto_stop_dev(backend_t *b) {
    b->running = 0;
    pthread_join(b->cap_thread, NULL);
}

static void pluto_close_dev(backend_t *b) {
    if (b->rxbuf) iio_buffer_destroy(b->rxbuf);
    if (b->ctx) iio_context_destroy(b->ctx);
    free(b);
}

const backend_ops_t backend_pluto = {
    .name = "PlutoSDR",
    .probe = pluto_probe_dev,
    .open = pluto_open_dev,
    .start = pluto_start_dev,
    .stop = pluto_stop_dev,
    .close = pluto_close_dev,
    .has_agc = 1,
    .dc_guard_bins = 4
};
