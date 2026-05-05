/**
 * @file freq_acq.c
 * @brief DSSS PRN-correlation frequency acquisition for OQPSK burst.
 *
 * Single-pass sweep at chip rate:
 *   2048-chip window (33 dB gain), 15 Hz step (±19 kHz).
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
#define PASS1_CORR_CHIPS  4096     /* 16 bits, 36 dB processing gain */
#define PASS1_STEP_HZ     4.0f     /* bin spacing (null at 9.4 Hz)    */
#define PASS1_CHIP_OFFS   3        /* coarse alignment positions      */
#define PASS1_OFFS_STEP   64       /* chips between positions         */

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
/* Build expected I-channel sign for 4 Costas phases (±1).            */
/* ------------------------------------------------------------------ */
static void build_expected_i(const int8_t *prn_i, const int8_t *prn_q,
                             int n,
                             float *e0, float *e1, float *e2, float *e3)
{
    for (int k = 0; k < n; k++) {
        float ie = 1.0f - 2.0f * (float)prn_i[k];   /* 0→+1, 1→-1 */
        float qe = 1.0f - 2.0f * (float)prn_q[k];
        e0[k] = ie;      /* Phase 0: I channel = I PRN            */
        e1[k] = qe;      /* Phase 1 (90°): swapped Q→I            */
        e2[k] = -ie;     /* Phase 2 (180°): inverted               */
        e3[k] = -qe;     /* Phase 3 (270°): swapped+inverted       */
    }
}

/* ------------------------------------------------------------------ */
/* I-channel-only correlation: sum Re(corrected[k]) * expected_I[k].  */
/* Avoids Q-channel cross-contamination from OQPSK phase mismatch.    */
/* ------------------------------------------------------------------ */
static float correlate_best_i(const float complex *corrected, int n_corr,
                              const float *e0, const float *e1,
                              const float *e2, const float *e3,
                              int *best_p)
{
    float c0 = 0.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f;

    for (int k = 0; k < n_corr; k++) {
        float re = __real__ corrected[k];
        c0 += re * e0[k];
        c1 += re * e1[k];
        c2 += re * e2[k];
        c3 += re * e3[k];
    }

    c0 = fabsf(c0);
    c1 = fabsf(c1);
    c2 = fabsf(c2);
    c3 = fabsf(c3);

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
                        const float *e0, const float *e1,
                        const float *e2, const float *e3,
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
        int p = 0;

        nco_rotate(chips, n_corr, f, chip_rate, corrected);
        float corr = correlate_best_i(corrected, n_corr, e0, e1, e2, e3, &p);

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

    int corr_chips = PASS1_CORR_CHIPS;
    if (n_chips < corr_chips) corr_chips = n_chips;

    int8_t *prn_i = (int8_t *)malloc((size_t)corr_chips);
    int8_t *prn_q = (int8_t *)malloc((size_t)corr_chips);
    if (!prn_i || !prn_q) { free(prn_i); free(prn_q); return -1; }

    despread_gen_prn(DESPREAD_PRN_SEED_I, corr_chips, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, corr_chips, prn_q);

    float *e0 = (float *)malloc((size_t)corr_chips * sizeof(float));
    float *e1 = (float *)malloc((size_t)corr_chips * sizeof(float));
    float *e2 = (float *)malloc((size_t)corr_chips * sizeof(float));
    float *e3 = (float *)malloc((size_t)corr_chips * sizeof(float));
    if (!e0 || !e1 || !e2 || !e3) {
        free(prn_i); free(prn_q);
        free(e0); free(e1); free(e2); free(e3);
        return -1;
    }
    build_expected_i(prn_i, prn_q, corr_chips, e0, e1, e2, e3);

    int n_steps = (int)((freq_max - freq_min) / PASS1_STEP_HZ) + 1;
    float best_f = 0.0f, best_conf = 0.0f;
    int   best_p = 0;

    for (int off = 0; off < PASS1_CHIP_OFFS; off++) {
        int chip_start = off * PASS1_OFFS_STEP;
        if (chip_start + corr_chips > n_chips) break;

        float f, conf;
        int p = 0;
        sweep_pass(chips + chip_start, n_chips - chip_start,
                   corr_chips, chip_rate,
                   e0, e1, e2, e3,
                   freq_min, PASS1_STEP_HZ, n_steps,
                   &f, &conf, &p);

        if (conf > best_conf) {
            best_conf = conf;
            best_f    = f;
            best_p    = p;
        }
    }

    free(prn_i); free(prn_q);
    free(e0); free(e1); free(e2); free(e3);

    result->freq_hz     = best_f;
    result->confidence  = best_conf;
    result->costas_phase = best_p;

    fprintf(stderr,
            "[freq_acq] sweep: offset %.0f Hz  conf %.1f  "
            "phase %d  chips=%d bins=%d\n",
            (double)result->freq_hz, (double)result->confidence,
            result->costas_phase, corr_chips, n_steps);

    return 0;
}

/* ================================================================== */
/*  Coarse FFT via 4th-power method (sample rate)                     */
/* ================================================================== */

#define COARSE_FFT_N  65536    /* ~34ms at 1.92MHz, ~27ms at 2.4576MHz */

static void fft_radix2(float complex *x, int n);  /* forward decl */

int freq_acq_coarse_fft(const float complex *samples, int n_samples,
                        float fs, float chip_rate,
                        freq_acq_result_t *result)
{
    if (!samples || n_samples < COARSE_FFT_N || !result)
        return -1;

    memset(result, 0, sizeof(*result));

    int n = COARSE_FFT_N;
    float complex *fft_buf = (float complex *)calloc((size_t)n,
                                                      sizeof(float complex));
    if (!fft_buf) return -1;

    /* Normalise and raise to 4th power: (I+jQ)^4 collapses OQPSK ±1±j */
    float scale = 0.0f;
    for (int i = 0; i < n; i++) {
        float re = __real__ samples[i];
        float im = __imag__ samples[i];
        float mag = re * re + im * im;
        if (mag > scale) scale = mag;
    }
    scale = scale > 0.0f ? 1.0f / scale : 1.0f;

    for (int i = 0; i < n; i++) {
        float re = __real__ samples[i] * scale;
        float im = __imag__ samples[i] * scale;
        /* s² = (re+j*im)² */
        float s2_re = re * re - im * im;
        float s2_im = 2.0f * re * im;
        /* s⁴ = (s²)² */
        __real__ fft_buf[i] = s2_re * s2_re - s2_im * s2_im;
        __imag__ fft_buf[i] = 2.0f * s2_re * s2_im;
    }

    fft_radix2(fft_buf, n);

    /* Magnitude */
    for (int i = 0; i < n; i++) {
        float re = __real__ fft_buf[i], im = __imag__ fft_buf[i];
        __real__ fft_buf[i] = re * re + im * im;
    }

    /* Find peak — skip DC (0 Hz ± guard) */
    int dc_guard = (int)((500.0f * (float)n) / fs);  /* ±500 Hz guard */
    if (dc_guard < 5) dc_guard = 5;
    float peak_mag = 0.0f;
    int   peak_bin = 0;
    float noise_sum = 0.0f;
    int   noise_cnt = 0;

    for (int i = dc_guard; i < n / 2; i++) {
        float mag = __real__ fft_buf[i];
        noise_sum += mag;
        noise_cnt++;
        if (mag > peak_mag) { peak_mag = mag; peak_bin = i; }
    }
    float noise_mean = noise_cnt > 0 ? noise_sum / (float)noise_cnt : 1e-10f;
    float ratio = peak_mag / noise_mean;

    /* Quadratic interpolation for sub-bin accuracy */
    float bin_f = (float)peak_bin;
    if (peak_bin > dc_guard && peak_bin < n / 2 - 1) {
        float L = __real__ fft_buf[peak_bin - 1];
        float C = peak_mag;
        float R = __real__ fft_buf[peak_bin + 1];
        float denom = 2.0f * C - L - R;
        if (denom > 1e-10f)
            bin_f += 0.5f * (R - L) / denom;
    }

    /* Peak is at 4×(fo + chip_rate/2) due to half-sine pulse shaping.
     * → fo = bin_f * fs / n / 4 - chip_rate / 2                      */
    float fo_raw  = bin_f * fs / ((float)n * 4.0f);
    float fo_corr = fo_raw - chip_rate * 0.5f;

    free(fft_buf);

    if (ratio < 5.0f)  /* require strong peak */
        return 0;      /* result stays at 0 Hz */

    result->freq_hz     = fo_corr;
    result->confidence  = ratio;
    result->costas_phase = 0;

    fprintf(stderr,
            "[freq_acq] coarse fft: raw=%.0f Hz  corr=%.0f Hz  "
            "conf=%.1f  peak_bin=%d\n",
            (double)fo_raw, (double)fo_corr, (double)ratio, peak_bin);

    return 0;
}

#define ALIGN_FFT_N            8192
#define ALIGN_PREAMBLE_CHIPS   6400

static void fft_radix2(float complex *x, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float complex t = x[i]; x[i] = x[j]; x[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float a = -2.0f * (float)M_PI / (float)len;
        float complex wlen = cosf(a) + sinf(a) * I;
        for (int i = 0; i < n; i += len) {
            float complex w = 1.0f;
            for (int j = 0; j < len / 2; j++) {
                float complex u = x[i + j];
                float complex v = x[i + j + len / 2] * w;
                x[i + j]           = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

int freq_acq_from_alignment(const float complex *chips, int n_chips,
                            const despread_sync_t *sync,
                            float chip_rate,
                            freq_acq_result_t *result)
{
    if (!chips || !sync || !result) return -1;
    if (sync->off_i < 0 || sync->off_q < 0) return -1;

    memset(result, 0, sizeof(*result));

    int8_t *prn_i = (int8_t *)malloc((size_t)ALIGN_PREAMBLE_CHIPS);
    int8_t *prn_q = (int8_t *)malloc((size_t)ALIGN_PREAMBLE_CHIPS);
    float complex *fft_buf = (float complex *)calloc((size_t)ALIGN_FFT_N,
                                                      sizeof(float complex));
    if (!prn_i || !prn_q || !fft_buf) {
        free(prn_i); free(prn_q); free(fft_buf);
        return -1;
    }

    despread_gen_prn(DESPREAD_PRN_SEED_I, ALIGN_PREAMBLE_CHIPS, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, ALIGN_PREAMBLE_CHIPS, prn_q);

    /* Strip PRN from I-channel only (avoids off_i≠off_q mismatch).
     * The preamble is all-zero bits, so I chips = PRN_I (no inversion). */
    int p = sync->phase & 3;
    float dc_sum = 0.0f;
    for (int k = 0; k < ALIGN_PREAMBLE_CHIPS; k++) {
        int ci = sync->off_i + k;
        float ir = (ci >= 0 && ci < n_chips) ? __real__ chips[ci] : 0.0f;

        /* Expected I value for this Costas phase. */
        float exp_i;
        if (p == 0)      exp_i = 1.0f - 2.0f * (float)prn_i[k];
        else if (p == 1) exp_i = 1.0f - 2.0f * (float)prn_q[k];   /* 90° swap */
        else if (p == 2) exp_i = -(1.0f - 2.0f * (float)prn_i[k]); /* 180° invert */
        else             exp_i = -(1.0f - 2.0f * (float)prn_q[k]); /* 270° swap+invert */

        __real__ fft_buf[k] = ir * exp_i;
        __imag__ fft_buf[k] = 0.0f;
        dc_sum += __real__ fft_buf[k];
    }

    /* DC removal. */
    float dc_mean = dc_sum / (float)ALIGN_PREAMBLE_CHIPS;
    for (int k = 0; k < ALIGN_PREAMBLE_CHIPS; k++)
        __real__ fft_buf[k] -= dc_mean;

    fft_radix2(fft_buf, ALIGN_FFT_N);

    /* Magnitude spectrum. */
    for (int i = 0; i < ALIGN_FFT_N; i++) {
        float re = __real__ fft_buf[i], im = __imag__ fft_buf[i];
        __real__ fft_buf[i] = re * re + im * im;
    }

    /* Find peak beyond DC guard. */
    float peak_mag = 0.0f;
    int   peak_bin = 0;
    float noise_sum = 0.0f;
    int   noise_cnt = 0;
    int   dc_guard  = 30;

    for (int i = 0; i < ALIGN_FFT_N / 2; i++) {
        float mag = __real__ fft_buf[i];
        if (i >= dc_guard) {
            if (mag > peak_mag) { peak_mag = mag; peak_bin = i; }
            noise_sum += mag; noise_cnt++;
        }
    }
    float noise_mean = noise_cnt > 0 ? noise_sum / (float)noise_cnt : 1e-10f;
    float ratio = peak_mag / noise_mean;

    /* Quadratic interpolation. */
    float bin_f = (float)peak_bin;
    if (peak_bin > dc_guard && peak_bin < ALIGN_FFT_N / 2 - 1) {
        float L = __real__ fft_buf[peak_bin - 1];
        float C = peak_mag;
        float R = __real__ fft_buf[peak_bin + 1];
        float denom = 2.0f * C - L - R;
        if (denom > 1e-10f)
            bin_f += 0.5f * (R - L) / denom;
    }
    float est_hz = bin_f * chip_rate / (float)ALIGN_FFT_N;

    free(prn_i); free(prn_q); free(fft_buf);

    if (peak_bin <= dc_guard + 5 || ratio < 3.0f)
        return 0;  /* no usable peak, result stays at 0 Hz */

    result->freq_hz     = est_hz;
    result->confidence  = ratio;
    result->costas_phase = p;

    fprintf(stderr,
            "[freq_acq] align fft: offset %.0f Hz  conf %.1f  "
            "peak_bin=%d phase=%d\n",
            (double)est_hz, (double)ratio, peak_bin, p);

    return 0;
}
