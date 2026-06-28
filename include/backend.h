#ifndef BACKEND_H
#define BACKEND_H

#include "scanner.h"
#include <stdint.h>

typedef struct backend backend_t;

typedef struct {
    const char *name;
    int (*probe)(void);
    backend_t *(*open)(uint32_t freq_hz, uint32_t *samp_rate, int gain, int extra);
    int (*start)(backend_t *b, scanner_t *s);
    void (*stop)(backend_t *b);
    void (*close)(backend_t *b);
    int dc_guard_bins;
} backend_ops_t;

#ifdef HAVE_RTLSDR
extern const backend_ops_t backend_rtlsdr;
#endif

#ifdef HAVE_AIRSPY
extern const backend_ops_t backend_airspy;
#endif

#ifdef HAVE_PLUTO
extern const backend_ops_t backend_pluto;
#endif

#endif
