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
#include <unistd.h>
#include <rtl-sdr.h>

#define SAMP_RATE 2457600u
#define CAP_CHUNK (1u << 17)

struct backend {
    rtlsdr_dev_t *dev;
    scanner_t *scanner;
    pthread_t cap_thread;
    int running;
    int gain;
    int ppm;
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

static void *capture_loop(void *arg) {
    backend_t *b = (backend_t *)arg;
    uint8_t buf[CAP_CHUNK];

    while (b->running) {
        int n_read = 0;
        int rc = rtlsdr_read_sync(b->dev, buf, (int)CAP_CHUNK, &n_read);
        if (rc < 0 || n_read <= 0) break;

        size_t ns = (size_t)n_read / 2;
        float complex tmp[ns];
        for (size_t i = 0; i < ns; i++) {
            float si = ((float)buf[2*i] - 127.5f) / 127.5f;
            float sq = ((float)buf[2*i+1] - 127.5f) / 127.5f;
            tmp[i] = si + sq * I;
        }
        scanner_push(b->scanner, tmp, ns);
    }
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
