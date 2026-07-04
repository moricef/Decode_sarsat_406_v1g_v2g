#define _GNU_SOURCE
#include "backend.h"
#include "diag_log.h"
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <rtl-sdr.h>

#define SAMP_RATE 2457600u
#define RTL_ASYNC_BUF_NUM 32u
#define RTL_ASYNC_BUF_LEN (1u << 18)

struct backend {
    rtlsdr_dev_t *dev;
    scanner_t *scanner;
    pthread_t cap_thread;
    int running;
    int gain;
    int ppm;
    float complex *tmp;
    size_t tmp_cap;
    int diag;
    uint64_t diag_total;
    struct timespec diag_t0;
    struct timespec diag_last;
};

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
    if (!p) return;
    char line[256];
    int done = 0;
    while (fgets(line, sizeof line, p)) {
        unsigned bus, dev, vid, pid;
        if (sscanf(line, "Bus %u Device %u: ID %x:%x", &bus, &dev, &vid, &pid) == 4 &&
            vid == 0x0bda && (pid == 0x2832 || pid == 0x2838)) {
            char path[64];
            snprintf(path, sizeof path, "/dev/bus/usb/%03u/%03u", bus, dev);
            int fd = open(path, O_WRONLY);
            if (fd >= 0) {
                if (ioctl(fd, USBDEVFS_RESET, 0) == 0) {
                    DIAG("  reset RTL-SDR USB (%s)\n", path);
                    done = 1;
                }
                close(fd);
            }
        }
    }
    pclose(p);
    if (done) {
        DIAG("  waiting for re-enumeration...\n");
        usleep(2000000);
    }
}

static int rtlsdr_probe(void) {
    int saved = silence_stderr_begin();
    int n = rtlsdr_get_device_count();
    silence_stderr_end(saved);
    return n > 0;
}

static backend_t *rtlsdr_open_dev(uint32_t freq_hz, uint32_t *samp_rate,
                                   int gain, int ppm) {
    reset_rtl_usb();

    backend_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->gain = gain;
    b->ppm = ppm;
    b->diag = getenv("RTL_DIAG") != NULL;

    int saved = silence_stderr_begin();
    int rc = rtlsdr_open(&b->dev, 0);
    silence_stderr_end(saved);
    if (rc < 0) { free(b); return NULL; }

    saved = silence_stderr_begin();
    rtlsdr_set_sample_rate(b->dev, SAMP_RATE);
    rtlsdr_set_center_freq(b->dev, freq_hz);
    rtlsdr_set_freq_correction(b->dev, ppm);
    if (gain >= 0) {
        rtlsdr_set_tuner_gain_mode(b->dev, 1);
        rtlsdr_set_tuner_gain(b->dev, gain * 10);
    } else {
        rtlsdr_set_tuner_gain_mode(b->dev, 0);
    }
    rtlsdr_reset_buffer(b->dev);
    silence_stderr_end(saved);

    *samp_rate = SAMP_RATE;
    return b;
}

static void rtlsdr_async_cb(unsigned char *buf, uint32_t len, void *ctx) {
    backend_t *b = (backend_t *)ctx;
    if (!b->running || !buf || len < 2) return;

    size_t ns = (size_t)len / 2;
    if (ns > b->tmp_cap) {
        float complex *tmp = realloc(b->tmp, ns * sizeof(float complex));
        if (!tmp) {
            b->running = 0;
            rtlsdr_cancel_async(b->dev);
            scanner_stop(b->scanner);
            return;
        }
        b->tmp = tmp;
        b->tmp_cap = ns;
    }

    if (b->diag) {
        b->diag_total += ns;
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC, &tn);
        if (tn.tv_sec - b->diag_last.tv_sec >= 5) {
            double el = (tn.tv_sec - b->diag_t0.tv_sec)
                      + (tn.tv_nsec - b->diag_t0.tv_nsec) / 1e9;
            DIAG("[rtl] throughput %.0f S/s (nominal %u, elapsed %.1fs)\n",
                 (double)b->diag_total / el, SAMP_RATE, el);
            b->diag_last = tn;
        }
    }

    for (size_t i = 0; i < ns; i++) {
        float si = ((float)buf[2*i] - 127.5f) / 127.5f;
        float sq = ((float)buf[2*i+1] - 127.5f) / 127.5f;
        b->tmp[i] = si + sq * I;
    }
    scanner_push(b->scanner, b->tmp, ns);
}

static void *capture_loop(void *arg) {
    backend_t *b = (backend_t *)arg;
    b->tmp_cap = RTL_ASYNC_BUF_LEN / 2;
    b->tmp = malloc(b->tmp_cap * sizeof(float complex));
    if (!b->tmp) { b->running = 0; scanner_stop(b->scanner); return NULL; }

    if (b->diag) {
        clock_gettime(CLOCK_MONOTONIC, &b->diag_t0);
        b->diag_last = b->diag_t0;
        b->diag_total = 0;
    }

    int rc = rtlsdr_read_async(b->dev, rtlsdr_async_cb, b,
                               RTL_ASYNC_BUF_NUM, RTL_ASYNC_BUF_LEN);
    if (rc < 0 && b->running)
        DWARN("WARNING: RTL async read failed (%d)\n", rc);

    free(b->tmp);
    b->tmp = NULL;
    b->tmp_cap = 0;
    b->running = 0;
    scanner_stop(b->scanner);
    return NULL;
}

static int rtlsdr_start_dev(backend_t *b, scanner_t *s) {
    b->scanner = s;
    b->running = 1;
    return pthread_create(&b->cap_thread, NULL, capture_loop, b);
}

static void rtlsdr_stop_dev(backend_t *b) {
    b->running = 0;
    rtlsdr_cancel_async(b->dev);
    pthread_join(b->cap_thread, NULL);
}

static void rtlsdr_close_dev(backend_t *b) {
    int saved = silence_stderr_begin();
    rtlsdr_close(b->dev);
    silence_stderr_end(saved);
    free(b);
}

const backend_ops_t backend_rtlsdr = {
    .name = "RTL-SDR",
    .probe = rtlsdr_probe,
    .open = rtlsdr_open_dev,
    .start = rtlsdr_start_dev,
    .stop = rtlsdr_stop_dev,
    .close = rtlsdr_close_dev,
    .dc_guard_bins = 10
};
