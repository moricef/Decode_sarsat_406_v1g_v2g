/**
 * @file rrc_filter.c
 * @brief Root Raised Cosine matched filter
 *
 * Port of gnuradio/gr-filter/lib/firdes.cc::root_raised_cosine().
 * Computes taps in double precision then casts to float, matching GR.
 */

#include "rrc_filter.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int rrc_compute_taps(float gain, float samp_rate, float sym_rate,
                     float alpha, int ntaps, float *taps)
{
    if (taps == NULL || ntaps < 1 || samp_rate <= 0.0f || sym_rate <= 0.0f)
        return 0;

    ntaps |= 1;  /* GR forces odd */

    double spb = (double)samp_rate / (double)sym_rate;  /* samples per baud */
    double a = (double)alpha;

    /*
     * Compute taps (double precision). GR's exact formulation from firdes.cc:
     *   xindx = i - ntaps/2
     *   x1 = pi * xindx / spb
     *   x2 = 4*alpha * xindx / spb
     *   x3 = x2*x2 - 1
     *
     *   if |x3| >= 1e-6:
     *     if i != ntaps/2:
     *       num = (cos((1+a)*x1) + sin((1-a)*x1)/(4*a*xindx/spb)) * 4*a
     *     else:
     *       num = cos((1+a)*x1) + (1-a)*pi/(4*a)
     *     den = x3 * pi
     *   else:                       (* near singularity at t = +/- T/(4*alpha) *)
     *     if a == 1: tap = -1, continue
     *     x3 = (1-a)*x1; x2 = (1+a)*x1
     *     num = sin(x2)*(1 + 1/(4*a)) + cos(x3)*((1 - 1/(4*a))*pi/(4*a))
     *     den = -32*pi*a*a*xindx/spb
     *
     *   tap = 4*a*num/den
     *
     * Then normalize by sum so total DC gain = `gain`.
     */
    double scale = 0.0;
    /* Use a small static cache for double-precision taps to keep
     * allocation simple. Max ntaps we expect is 705. Caller-provided
     * float buffer holds final values. */
    /* For a generic implementation, do it in two passes: first compute
     * doubles into the float[] buffer (re-interpreting), then normalize.
     * Simpler: compute doubles on the fly, accumulate scale, store float. */
    /* To match GR exactly we keep doubles until normalization. */
    static double dbuf[8192];  /* enough for 11*sps with sps up to ~700 */
    if (ntaps > (int)(sizeof(dbuf) / sizeof(dbuf[0])))
        return 0;

    for (int i = 0; i < ntaps; i++) {
        double xindx = (double)(i - ntaps / 2);
        double x1 = M_PI * xindx / spb;
        double x2 = 4.0 * a * xindx / spb;
        double x3 = x2 * x2 - 1.0;
        double num, den, tap;

        if (fabs(x3) >= 1e-6) {
            if (i != ntaps / 2) {
                num = cos((1.0 + a) * x1) +
                      sin((1.0 - a) * x1) / (4.0 * a * xindx / spb);
            } else {
                num = cos((1.0 + a) * x1) + (1.0 - a) * M_PI / (4.0 * a);
            }
            den = x3 * M_PI;
        } else {
            if (a == 1.0) {
                dbuf[i] = -1.0;
                scale += dbuf[i];
                continue;
            }
            x3 = (1.0 - a) * x1;
            x2 = (1.0 + a) * x1;
            num = sin(x2) * (1.0 + a) * M_PI -
                  cos(x3) * ((1.0 - a) * M_PI * spb) / (4.0 * a * xindx) +
                  sin(x3) * spb * spb / (4.0 * a * xindx * xindx);
            den = -32.0 * M_PI * a * a * xindx / spb;
        }

        tap = 4.0 * a * num / den;
        dbuf[i] = tap;
        scale += tap;
    }

    /* Normalize for DC gain */
    double inv = (double)gain / scale;
    for (int i = 0; i < ntaps; i++)
        taps[i] = (float)(dbuf[i] * inv);

    return ntaps;
}

void rrc_filter_complex(const float complex *input, size_t input_length,
                        const float *taps, int ntaps,
                        float complex *output)
{
    if (input == NULL || taps == NULL || output == NULL || ntaps < 1)
        return;

    int half = ntaps / 2;

    for (size_t i = 0; i < input_length; i++) {
        float complex acc = 0.0f + 0.0f * I;
        /* Output[i] = sum_{j=0..ntaps-1} taps[j] * input[i + j - half] */
        long start_j = (long)half - (long)i;          /* j s.t. input idx = 0 */
        long end_j = (long)(input_length + half) - (long)i;  /* j s.t. input idx = input_length */
        if (start_j < 0) start_j = 0;
        if (end_j > ntaps) end_j = ntaps;

        long base = (long)i - (long)half;
        for (long j = start_j; j < end_j; j++) {
            acc += input[base + j] * taps[j];
        }
        output[i] = acc;
    }
}
