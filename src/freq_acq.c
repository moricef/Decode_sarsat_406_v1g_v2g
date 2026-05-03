/**
 * @file freq_acq.c
 * @brief DSSS PRN-correlation frequency acquisition for OQPSK burst.
 *
 * Two-pass hierarchical sweep operating at chip rate:
 *   Pass 1 — 256-chip window, 100 Hz step (±30 kHz), 3 chip offsets.
 *   Pass 2 — 2048-chip window,  10 Hz step (±150 Hz around winner).
 *
 * NCO correction uses a rotating phasor (no cos/sin in the inner loop).
 * The 4 Costas-phase ambiguities are tested at each frequency.
 */

#include "freq_acq.h"
#include "despread.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Sweep parameters                                                   */
/* ------------------------------------------------------------------ */
#define PASS1_CORR_CHIPS  512      /* 2 bits, 27 dB processing gain  */
#define PASS1_STEP_HZ     100.0f   /* bin spacing (~34 Hz 3dB bw)  */
#define PASS1_CHIP_OFFS   3        /* coarse alignment positions   */
#define PASS1_OFFS_STEP   64       /* chips between positions      */

#define PASS2_CORR_CHIPS  2048     /* 8 bits, 33 dB processing gain */
#define PASS2_STEP_HZ     10.0f    /* bin spacing                   */
#define PASS2_SWEEP_HZ    150.0f   /* sweep width around coarse     */

#define PELL_THRESH       1.8f     /* confidence floor              */

/* ------------------------------------------------------------------ */
/* Helper: complex multiply (inlined for speed).                       */
/* ------------------------------------------------------------------ */
static inline float complex cmul_ro(float complex a, float complex b)
{
    float re = __real__ a * __real__ b - __imag__ a * __imag__ b;
    float im = __real__ a * __imag__ b + __imag__ a * __real__ b;
    return re + im * I;
}

/* ------------------------------------------------------------------ */
/* Apply NCO rotation to a single chip using a live phasor.           */
/*   phasor_k = exp(-j * 2π * freq * k / chip_rate)                   */
/*   step     = exp(-j * 2π * freq / chip_rate)                       */
/* ------------------------------------------------------------------ */
static void nco_rotate(const float complex *in, int n,
                       float freq_hz, float chip_rate,
                       float complex *out)
{
    float dphi = -(2.0f * (float)M_PI * freq_hz / chip_rate);
    float step_r = cosf(dphi);
    float step_i = sinf(dphi);
    float ph_r = 1.0f, ph_i = 0.0f;
    for (int k = 0; k < n; k++) {
        float re = __real__ in[k], im = __imag__ in[k];
        __real__ out[k] = re * ph_r - im * ph_i;
        __imag__ out[k] = re * ph_i + im * ph_r;
        float nr = ph_r * step_r - ph_i * step_i;
        float ni = ph_r * step_i + ph_i * step_r;
        if ((k & 1023) == 0) {  /* periodic renormalisation */
            float inv = 1.0f / sqrtf(nr * nr + ni * ni);
            nr *= inv; ni *= inv;
        }
        ph_r = nr; ph_i = ni;
    }
}

/* ------------------------------------------------------------------ */
/* Build expected ±1 I/Q patterns for the 4 Costas phases.            */
/*   prn_i/q are 0/1 arrays from despread_gen_prn().                  */
/*   expected[phase][k] = exp_i[k] + j*exp_q[k]  with  (±1, ±1).     */
/* ------------------------------------------------------------------ */
static void build_expected_4(const int8_t *prn_i, const int8_t *prn_q,
                             int n,
                             float complex *e0, float complex *e1,
                             float complex *e2, float complex *e3)
{
    for (int k = 0; k < n; k++) {
        float ie = 1.0f - 2.0f * (float)prn_i[k];   /* 0→+1, 1→-1 */
        float qe = 1.0f - 2.0f * (float)prn_q[k];

        /* Phase 0: I=ie, Q=qe  (1+1j convention) */
        e0[k] = ie + qe * I;

        /* Phase 1 (90°): I=qe, Q=-ie */
        e1[k] = qe - ie * I;

        /* Phase 2 (180°): I=-ie, Q=-qe */
        e2[k] = -ie - qe * I;

        /* Phase 3 (270°): I=-qe, Q=ie */
        e3[k] = -qe + ie * I;
    }
}

/* ------------------------------------------------------------------ */
/* Correlate corrected[n_corr] (pre-rotated by NCO) against the       */
/* expected pattern at all 4 Costas phases. Returns the best absolute */
/* correlation value and the winning phase.                           */
/* ------------------------------------------------------------------ */
static float correlate_best(const float complex *corrected, int n_corr,
                            const float complex *e0,
                            const float complex *e1,
                            const float complex *e2,
                            const float complex *e3,
                            int *best_p)
{
    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;
    float r0r = 0.0f, r0i = 0.0f, r1r = 0.0f, r1i = 0.0f;
    float r2r = 0.0f, r2i = 0.0f, r3r = 0.0f, r3i = 0.0f;

    for (int k = 0; k < n_corr; k++) {
        float re = __real__ corrected[k];
        float im = __imag__ corrected[k];

        /* Phase 0 — dot product: dot(corrected, conj(expected)) = Re(corr*conj(exp))
         * We accumulate Re and Im of corr[k] = corrected[k] * conj(expected[k]),
         * then the final correlation value is |sum(corr[k])|.
         * But conj(expected[k]): e0 = exp_i + j*exp_q, conj = exp_i - j*exp_q.
         * corr = corrected * conj: (re+j*im)*(exp_i-j*exp_q) = (re*exp_i+im*exp_q)+j*(im*exp_i-re*exp_q)
         */
        float ee_i = __real__ e0[k], ee_q = __imag__ e0[k];
        r0r += re * ee_i + im * ee_q;
        r0i += im * ee_i - re * ee_q;

        ee_i = __real__ e1[k]; ee_q = __imag__ e1[k];
        r1r += re * ee_i + im * ee_q;
        r1i += im * ee_i - re * ee_q;

        ee_i = __real__ e2[k]; ee_q = __imag__ e2[k];
        r2r += re * ee_i + im * ee_q;
        r2i += im * ee_i - re * ee_q;

        ee_i = __real__ e3[k]; ee_q = __imag__ e3[k];
        r3r += re * ee_i + im * ee_q;
        r3i += im * ee_i - re * ee_q;
    }

    c0 = sqrtf(r0r * r0r + r0i * r0i);
    c1 = sqrtf(r1r * r1r + r1i * r1i);
    c2 = sqrtf(r2r * r2r + r2i * r2i);
    c3 = sqrtf(r3r * r3r + r3i * r3i);

    float best = c0; *best_p = 0;
    if (c1 > best) { best = c1; *best_p = 1; }
    if (c2 > best) { best = c2; *best_p = 2; }
    if (c3 > best) { best = c3; *best_p = 3; }
    return best;
}

/* ------------------------------------------------------------------ */
/* Single-pass sweep.                                                 */
/* ------------------------------------------------------------------ */
static float sweep_pass(const float complex *chips, int n_chips, int n_corr,
                        float chip_rate,
                        const float complex *e0,
                        const float complex *e1,
                        const float complex *e2,
                        const float complex *e3,
                        float f_start, float f_step, int n_steps,
                        float *best_f, float *best_conf, int *best_p)
{
    *best_f    = 0.0f;
    *best_conf = 0.0f;

    float complex *corrected = (float complex *)malloc(
        (size_t)n_corr * sizeof(float complex));
    if (!corrected) return -1.0f;

    float peak = 0.0f, sum = 0.0f;
    int   cnt = 0;

    for (int s = 0; s < n_steps; s++) {
        float f = f_start + (float)s * f_step;
        int p;

        nco_rotate(chips, n_corr, f, chip_rate, corrected);
        float corr = correlate_best(corrected, n_corr, e0, e1, e2, e3, &p);

        sum += corr; cnt++;
        if (corr > peak) {
            peak = corr;
            *best_f  = f;
            *best_p  = p;
        }
    }

    float mean = cnt > 0 ? sum / (float)cnt : 1e-10f;
    *best_conf = peak / mean;

    free(corrected);
    return peak;
}

/* ------------------------------------------------------------------ */
/* Public API.                                                         */
/* ------------------------------------------------------------------ */

int freq_acq_sweep(const float complex *chips, int n_chips,
                   float chip_rate,
                   float freq_min, float freq_max,
                   freq_acq_result_t *result)
{
    if (!chips || n_chips < PASS1_CORR_CHIPS || !result)
        return -1;

    memset(result, 0, sizeof(*result));

    /* Build expected PRN patterns once (Pass-2 length = max needed). */
    int max_corr = PASS2_CORR_CHIPS;
    if (n_chips < max_corr) max_corr = n_chips;

    int8_t *prn_i = (int8_t *)malloc((size_t)max_corr);
    int8_t *prn_q = (int8_t *)malloc((size_t)max_corr);
    if (!prn_i || !prn_q) { free(prn_i); free(prn_q); return -1; }

    despread_gen_prn(DESPREAD_PRN_SEED_I, max_corr, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, max_corr, prn_q);

    float complex *e0 = (float complex *)malloc((size_t)max_corr * sizeof(float complex));
    float complex *e1 = (float complex *)malloc((size_t)max_corr * sizeof(float complex));
    float complex *e2 = (float complex *)malloc((size_t)max_corr * sizeof(float complex));
    float complex *e3 = (float complex *)malloc((size_t)max_corr * sizeof(float complex));
    if (!e0 || !e1 || !e2 || !e3) {
        free(prn_i); free(prn_q);
        free(e0); free(e1); free(e2); free(e3);
        return -1;
    }
    build_expected_4(prn_i, prn_q, max_corr, e0, e1, e2, e3);

    /* ---------------------------------------------------------------
     * Pass 1 — coarse sweep, 256-chip correlation.
     * Try 3 chip offsets to tolerate burst-alignment uncertainty.
     * --------------------------------------------------------------- */
    int n_steps1 = (int)((freq_max - freq_min) / PASS1_STEP_HZ) + 1;
    float best_f1 = 0.0f, best_conf1 = 0.0f;
    int best_p1 = 0;

    for (int off = 0; off < PASS1_CHIP_OFFS; off++) {
        int chip_start = off * PASS1_OFFS_STEP;
        if (chip_start + PASS1_CORR_CHIPS > n_chips) break;

        float f, conf;
        int p;
        sweep_pass(chips + chip_start, n_chips - chip_start,
                   PASS1_CORR_CHIPS, chip_rate,
                   e0, e1, e2, e3,
                   freq_min, PASS1_STEP_HZ, n_steps1,
                   &f, &conf, &p);

        if (conf > best_conf1) {
            best_conf1 = conf;
            best_f1    = f;
            best_p1    = p;
        }
    }

    /* ---------------------------------------------------------------
     * Pass 2 — fine sweep, 2048-chip correlation, ±150 Hz around best.
     * --------------------------------------------------------------- */
    float f2_start = best_f1 - PASS2_SWEEP_HZ;
    float f2_end   = best_f1 + PASS2_SWEEP_HZ;
    int   n_steps2 = (int)((f2_end - f2_start) / PASS2_STEP_HZ) + 1;

    float best_f2 = best_f1, best_conf2 = best_conf1;
    int   best_p2 = best_p1;

    if (max_corr >= PASS2_CORR_CHIPS && best_conf1 > PELL_THRESH) {
        sweep_pass(chips, n_chips,
                   PASS2_CORR_CHIPS, chip_rate,
                   e0, e1, e2, e3,
                   f2_start, PASS2_STEP_HZ, n_steps2,
                   &best_f2, &best_conf2, &best_p2);
    }

    /* --------------------------------------------------------------- */
    free(prn_i); free(prn_q);
    free(e0); free(e1); free(e2); free(e3);

    result->freq_hz     = best_f2;
    result->confidence  = best_conf2;
    result->costas_phase = best_p2;

    fprintf(stderr,
            "[freq_acq] sweep done: offset %.0f Hz  conf %.1f  "
            "phase %d  (pass1 %.0f Hz conf %.1f)\n",
            (double)result->freq_hz, (double)result->confidence,
            result->costas_phase,
            (double)best_f1, (double)best_conf1);

    return 0;
}
