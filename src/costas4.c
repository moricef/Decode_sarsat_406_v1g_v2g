/**
 * @file costas4.c
 * @brief 4th-order Costas loop for QPSK carrier recovery
 *
 * Verbatim port of:
 *   - gr-blocks/lib/control_loop.cc (constructor + update_gains)
 *   - gr-digital/lib/costas_loop_cc_impl.cc (work loop, phase_detector_4)
 *   - gr-digital/lib/costas_loop_cc_impl.h (phase_detector_4)
 *   - gnuradio-runtime/include/gnuradio/math.h (branchless_clip)
 *   - gr-blocks/include/gnuradio/blocks/control_loop.h (advance_loop,
 *     phase_wrap, frequency_limit inline methods)
 */

#include "costas4.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_TWOPI (2.0f * (float)M_PI)

static inline float branchless_clip(float x, float clip)
{
    return 0.5f * (fabsf(x + clip) - fabsf(x - clip));
}

static inline float phase_detector_4(float re, float im)
{
    return ((re > 0.0f ? 1.0f : -1.0f) * im -
            (im > 0.0f ? 1.0f : -1.0f) * re);
}

void costas4_init(costas4_t *st, float loop_bw)
{
    if (st == NULL) return;
    st->phase = 0.0f;
    st->freq = 0.0f;
    st->damping = sqrtf(2.0f) / 2.0f;   /* GR default */
    st->loop_bw = loop_bw;
    st->max_freq = 1.0f;                /* GR default */
    st->min_freq = -1.0f;

    /* update_gains() */
    float denom = 1.0f + 2.0f * st->damping * loop_bw + loop_bw * loop_bw;
    st->alpha = (4.0f * st->damping * loop_bw) / denom;
    st->beta  = (4.0f * loop_bw * loop_bw) / denom;
}

void costas4_run(costas4_t *st, const float complex *in,
                 float complex *out, size_t n)
{
    if (st == NULL || in == NULL || out == NULL) return;

    float phase = st->phase;
    float freq  = st->freq;
    float alpha = st->alpha;
    float beta  = st->beta;
    float fmax  = st->max_freq;
    float fmin  = st->min_freq;

    for (size_t i = 0; i < n; i++) {
        /* nco_out = exp(-j*phase) → cos(phase) - j*sin(phase) */
        float c = cosf(phase);
        float s = sinf(phase);
        float xr = __real__ in[i];
        float xi = __imag__ in[i];
        /* y = x * nco_out */
        float yr = xr * c + xi * s;
        float yi = xi * c - xr * s;

        float err = phase_detector_4(yr, yi);
        err = branchless_clip(err, 1.0f);

        /* advance_loop */
        freq += beta * err;
        phase += freq + alpha * err;

        /* phase_wrap: keep in [-2*pi, 2*pi] */
        while (phase > M_TWOPI)  phase -= M_TWOPI;
        while (phase < -M_TWOPI) phase += M_TWOPI;

        /* frequency_limit */
        if (freq > fmax) freq = fmax;
        else if (freq < fmin) freq = fmin;

        out[i] = yr + I * yi;
    }

    st->phase = phase;
    st->freq = freq;
}
