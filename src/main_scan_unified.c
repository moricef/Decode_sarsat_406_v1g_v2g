/**
 * @file main_scan_unified.c
 * @brief dec406_scan — unified real-time FGB+SGB scanner with auto hardware detection.
 *
 * Detects connected SDR hardware (Airspy Mini, RTL-SDR, PlutoSDR, HackRF One) and uses the first
 * available backend. The spectral burst detector and FGB/SGB decoders are
 * shared across all backends via the scanner module.
 *
 * Usage: dec406_scan <freq_start> <freq_end> [gain] [extra]
 *   e.g. dec406_scan 406.0M 406.1M
 *        dec406_scan 431.9M 432.0M 15
 */

#define _GNU_SOURCE
#include "backend.h"
#include "diag_log.h"
#include "scan_alert.h"
#include "scanner.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void on_sigint(int s) { (void)s; running = 0; }

static double parse_freq(const char *s) {
    char *end;
    double v = strtod(s, &end);
    switch (*end) {
    case 'k': case 'K': v *= 1e3; break;
    case 'M': v *= 1e6; break;
    case 'G': v *= 1e9; break;
    }
    return v;
}

static const backend_ops_t *backends[] = {
#ifdef HAVE_AIRSPY
    &backend_airspy,
#endif
#ifdef HAVE_RTLSDR
    &backend_rtlsdr,
#endif
#ifdef HAVE_PLUTO
    &backend_pluto,
#endif
#ifdef HAVE_HACKRF
    &backend_hackrf,
#endif
    NULL
};

const backend_ops_t *backend_find_by_name(const char *name) {
    if (!name || !*name) return NULL;
    for (int i = 0; backends[i]; i++) {
        if (strcasecmp(name, backends[i]->name) == 0)
            return backends[i];
        if ((strcasecmp(name, "rtl") == 0) &&
            strcmp(backends[i]->name, "RTL-SDR") == 0)
            return backends[i];
        if ((strcasecmp(name, "airspy") == 0) &&
            strcmp(backends[i]->name, "Airspy Mini") == 0)
            return backends[i];
        if ((strcasecmp(name, "pluto") == 0) &&
            strcmp(backends[i]->name, "PlutoSDR") == 0)
            return backends[i];
        if ((strcasecmp(name, "hackrf") == 0 || strcasecmp(name, "hackrf one") == 0) &&
            strcmp(backends[i]->name, "HackRF One") == 0)
            return backends[i];
    }
    return NULL;
}

int main(int argc, char **argv) {
    /* Line-buffer stdout: on a pipe it would otherwise be block-buffered
     * while stderr never is, so `2>&1 | tee` interleaves decoder output
     * (printf) with diagnostics (DIAG, stderr) in the wrong order. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <freq_start> <freq_end> [gain] [extra]\n"
                "  e.g. %s 406.0M 406.1M\n"
                "       %s 431.9M 432.0M 15\n"
                "\n"
        "Set DEC406_BACKEND=rtl|airspy|pluto|hackrf to force a device.\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    double f1 = parse_freq(argv[1]);
    double f2 = parse_freq(argv[2]);
    if (f2 < f1) { double t = f1; f1 = f2; f2 = t; }
    int gain = (argc > 3) ? atoi(argv[3]) : -1;
    int extra = (argc > 4) ? atoi(argv[4]) : 0;

    double center_hz = (f1 + f2) / 2.0;
    double span = f2 - f1;

    printf("dec406_scan — unified FGB+SGB real-time decoder\n");
    printf("  band    : %.3f - %.3f MHz   (span %.0f kHz)\n",
           f1 / 1e6, f2 / 1e6, span / 1e3);
    printf("  center  : %.3f MHz\n", center_hz / 1e6);
    fflush(stdout);

    const char *forced = getenv("DEC406_BACKEND");
    const backend_ops_t *ops = NULL;

    if (forced && *forced) {
        ops = backend_find_by_name(forced);
        if (!ops) {
            fprintf(stderr, "ERROR: unsupported DEC406_BACKEND=%s\n", forced);
            return 1;
        }
        printf("  backend : forced by DEC406_BACKEND=%s\n", forced);
    } else {
        for (int i = 0; backends[i]; i++) {
            printf("  probing : %s ... ", backends[i]->name);
            fflush(stdout);
            if (backends[i]->probe()) {
                printf("found\n");
                ops = backends[i];
                break;
            }
            printf("not found\n");
        }
    }

    if (!ops) {
        fprintf(stderr, "ERROR: no supported SDR hardware found\n");
        return 1;
    }

    uint32_t samp_rate;
    backend_t *backend = ops->open((uint32_t)center_hz, &samp_rate, gain, extra);
    if (!backend) {
        fprintf(stderr, "ERROR: failed to open %s\n", ops->name);
        return 1;
    }

    const char *using_name = ops->model ? ops->model(backend) : NULL;
    printf("  using   : %s\n", using_name ? using_name : ops->name);

    printf("  rate    : %.4f MSPS   (SPS=%.1f for SGB)\n",
           samp_rate / 1e6, (double)samp_rate / 38400.0);
    if (gain >= 0)
        printf("  gain    : %d\n", gain);
    else if (ops->has_agc)
        printf("  gain    : auto (hardware AGC)\n");
    else
        printf("  gain    : backend default\n");
    if (strcmp(ops->name, "RTL-SDR") == 0)
        printf("  %-8s: %d\n", "ppm", extra);
    else if (strcmp(ops->name, "HackRF One") == 0)
        printf("  %-8s: %d\n", "amp", extra);
    else
        printf("  %-8s: %d\n", "extra", extra);

    if (span > samp_rate)
        DWARN("WARNING: band span %.0f Hz > sample rate %u Hz\n", span, samp_rate);

    int alerts_ok = (scan_alert_load_config("data/config_mail.txt") == 0);
    printf("  alerts  : %s\n", alerts_ok ? "enabled" : "disabled");
    if (alerts_ok) scan_alert_print_config_summary();
    printf("\n");
    fflush(stdout);

    scanner_t scanner;
    if (scanner_init(&scanner, samp_rate, center_hz, f1, f2, ops->dc_guard_bins) < 0) {
        fprintf(stderr, "ERROR: scanner_init failed\n");
        ops->close(backend);
        return 1;
    }

    printf("  ring    : %.0f MB   (~%.1f s)\n\n",
           (double)SCANNER_RING_SAMPLES * sizeof(float complex) / 1e6,
           (double)SCANNER_RING_SAMPLES / samp_rate);
    fflush(stdout);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (ops->start(backend, &scanner) != 0) {
        fprintf(stderr, "ERROR: backend start failed\n");
        scanner_free(&scanner);
        ops->close(backend);
        return 1;
    }

    pthread_t proc_t;
    pthread_create(&proc_t, NULL, (void *(*)(void *))scanner_process, &scanner);

    while (running) usleep(100000);

    scanner_stop(&scanner);
    ops->stop(backend);
    pthread_join(proc_t, NULL);

    ops->close(backend);
    printf("\nstopped — %lu ring overrun(s)\n", scanner.overruns);
    scanner_free(&scanner);

    return 0;
}
