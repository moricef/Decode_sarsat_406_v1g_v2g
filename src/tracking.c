/**
 * @file tracking.c
 * @brief FLL + PLL + DLL burst tracking loop for DSSS/OQPSK.
 *
 * Phase 2: FLL (cross-product discriminator) + DLL.
 *          Carrier NCO updated at each epoch via 2nd-order loop filter.
 *          Code NCO tracks chip boundaries via E-L normalized discriminator.
 *          EPL accumulates at every sample (not just chip boundaries).
 *
 * Architecture per Zhang et al. (ICEE 2025), Fig. 59.1.
 */

#include "tracking.h"
#include "despread.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Loop bandwidths (Hz) */
#define DLL_BW_ACQ  1.0f
#define FLL_BW_ACQ  2.0f

/* ------------------------------------------------------------------ */
/* Compute 1st-order code loop filter gains.                          */
/* alpha = 4*BW*T / (1 + 4*BW*T)                                     */
/* ------------------------------------------------------------------ */
static void compute_code_gains(tracking_state_t *trk, float bw)
{
    float T = (float)trk->coh_chips / trk->chip_rate;
    float x = 4.0f * bw * T;
    trk->code_alpha = x / (1.0f + x);
    trk->code_beta  = 0.0f;
}

/* ------------------------------------------------------------------ */
/* Compute 2nd-order carrier loop filter gains.                       */
/* wn = BW*8*zeta/(4*zeta²+1), zeta = 1/sqrt(2)                      */
/* alpha = sqrt(2)*wn*T, beta = wn²*T²                               */
/* ------------------------------------------------------------------ */
static void compute_carrier_gains(tracking_state_t *trk, float bw)
{
    float T = (float)trk->coh_chips / trk->chip_rate;
    float zeta = 0.70710678f;
    float wn = bw * 8.0f * zeta / (4.0f * zeta * zeta + 1.0f);
    trk->carr_alpha = 1.41421356f * wn * T;
    trk->carr_beta  = wn * wn * T * T;
}

/* ------------------------------------------------------------------ */
/* Get PRN chip sign (±1.0f) with bounds clamping.                     */
/* ------------------------------------------------------------------ */
static inline float prn_sign(const int8_t *prn, int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= DESPREAD_PRN_LEN) idx = DESPREAD_PRN_LEN - 1;
    return 1.0f - 2.0f * (float)prn[idx];
}

/* ------------------------------------------------------------------ */
/* DLL discriminator: normalized early-minus-late power.                */
/*   d_tau = 0.5 * (E - L) / (E + L)   [in chips]                     */
/* ------------------------------------------------------------------ */
static float dll_discriminator(const epl_accum_t *acc)
{
    float E = __real__ acc->early_i  * __real__ acc->early_i
            + __imag__ acc->early_i  * __imag__ acc->early_i
            + __real__ acc->early_q  * __real__ acc->early_q
            + __imag__ acc->early_q  * __imag__ acc->early_q;

    float L = __real__ acc->late_i   * __real__ acc->late_i
            + __imag__ acc->late_i   * __imag__ acc->late_i
            + __real__ acc->late_q   * __real__ acc->late_q
            + __imag__ acc->late_q   * __imag__ acc->late_q;

    float denom = E + L;
    if (denom < 1e-20f) return 0.0f;
    return 0.5f * (E - L) / denom;
}

/* ------------------------------------------------------------------ */
/* FLL discriminator: cross-product, insensitive to data modulation.   */
/*   cross = Re(P[k-1])*Im(P[k]) - Im(P[k-1])*Re(P[k])                */
/*   dot   = Re(P[k-1])*Re(P[k]) + Im(P[k-1])*Im(P[k])                */
/*   d_freq = atan2(cross, dot) / (2*pi*T)   [Hz]                     */
/* ------------------------------------------------------------------ */
static float fll_discriminator(float complex prev, float complex cur, float T)
{
    float cross = crealf(prev) * cimagf(cur) - cimagf(prev) * crealf(cur);
    float dot   = crealf(prev) * crealf(cur) + cimagf(prev) * cimagf(cur);
    return atan2f(cross, dot) / (2.0f * (float)M_PI * T);
}

/* ------------------------------------------------------------------ */
/* Process one completed epoch: discriminators + loop filter update.    */
/* ------------------------------------------------------------------ */
static void process_epoch(tracking_state_t *trk)
{
    float T = (float)trk->coh_chips / trk->chip_rate;

    /* DLL discriminator + 1st-order code loop filter */
    float d_tau = dll_discriminator(&trk->accum);
    trk->code_freq += trk->code_alpha * d_tau / trk->sps;

    /* Clamp code_freq to ±1% of nominal */
    float nominal = 1.0f / trk->sps;
    float max_dev = nominal * 0.01f;
    if (trk->code_freq > nominal + max_dev) trk->code_freq = nominal + max_dev;
    if (trk->code_freq < nominal - max_dev) trk->code_freq = nominal - max_dev;

    /* FLL discriminator + 2nd-order carrier loop filter */
    float dot_prev = crealf(trk->prev_prompt) * crealf(trk->prev_prompt)
                   + cimagf(trk->prev_prompt) * cimagf(trk->prev_prompt);
    if (dot_prev > 1e-12f) {
        float d_freq = fll_discriminator(trk->prev_prompt, trk->accum.prompt_i, T);
        trk->carr_integrator += trk->carr_beta * d_freq;
        float freq_hz = trk->carr_integrator + trk->carr_alpha * d_freq;
        trk->carrier_freq = 2.0f * (float)M_PI * freq_hz / trk->fs;
    }

    trk->prev_prompt = trk->accum.prompt_i;

    /* Debug trace */
    if (trk->dbg_code && trk->dbg_len < trk->dbg_cap) {
        trk->dbg_code[trk->dbg_len]  = d_tau;
        trk->dbg_freq[trk->dbg_len]  = trk->carrier_freq * trk->fs / (2.0f * (float)M_PI);
        trk->dbg_phase[trk->dbg_len] = trk->carrier_phase;
        trk->dbg_len++;
    }

    trk->epoch_count++;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int tracking_init(tracking_state_t *trk,
                  float fs, float sps,
                  float coarse_freq_hz,
                  float init_code_phase)
{
    if (!trk || sps < 4.0f || fs <= 0.0f)
        return -1;

    memset(trk, 0, sizeof(*trk));

    trk->fs        = fs;
    trk->sps       = sps;
    trk->chip_rate = fs / sps;

    trk->carrier_phase = 0.0f;
    trk->carrier_freq  = 2.0f * (float)M_PI * coarse_freq_hz / fs;

    trk->code_phase   = init_code_phase;
    trk->code_freq    = 1.0f / sps;
    trk->epl_spacing  = 0.5f;

    trk->state     = TRK_STATE_ACQ;
    trk->coh_chips = 64;

    compute_code_gains(trk, DLL_BW_ACQ);
    compute_carrier_gains(trk, FLL_BW_ACQ);

    trk->prn_i = (int8_t *)malloc(DESPREAD_PRN_LEN);
    trk->prn_q = (int8_t *)malloc(DESPREAD_PRN_LEN);
    if (!trk->prn_i || !trk->prn_q) {
        free(trk->prn_i); free(trk->prn_q);
        return -1;
    }
    despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PRN_LEN, trk->prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, DESPREAD_PRN_LEN, trk->prn_q);

    int max_epochs = (int)(3.0f * trk->chip_rate / (float)trk->coh_chips) + 100;
    trk->dbg_cap = max_epochs;
    trk->dbg_freq  = (float *)calloc((size_t)max_epochs, sizeof(float));
    trk->dbg_phase = (float *)calloc((size_t)max_epochs, sizeof(float));
    trk->dbg_code  = (float *)calloc((size_t)max_epochs, sizeof(float));
    trk->dbg_cn0   = (float *)calloc((size_t)max_epochs, sizeof(float));

    return 0;
}

int tracking_run(tracking_state_t *trk,
                 const float complex *samples,
                 size_t n_samples,
                 float complex *chips_out,
                 size_t *n_chips_out)
{
    if (!trk || !samples || !chips_out || !n_chips_out)
        return -1;

    /* Carrier NCO rotating phasor */
    float carr_dphi = trk->carrier_freq;
    float ph_r = cosf(trk->carrier_phase);
    float ph_i = sinf(trk->carrier_phase);
    float step_r = cosf(carr_dphi);
    float step_i = sinf(carr_dphi);

    float code_phase = trk->code_phase;
    float code_freq  = trk->code_freq;
    float epl_d      = trk->epl_spacing;

    size_t chip_out_idx = 0;
    int prev_peak = (int)(code_phase + 0.5f);

    memset(&trk->accum, 0, sizeof(trk->accum));

    for (size_t s = 0; s < n_samples; s++) {
        /* 1. Carrier wipeoff */
        float re_in = __real__ samples[s];
        float im_in = __imag__ samples[s];
        float yr = re_in * ph_r + im_in * ph_i;
        float yi = im_in * ph_r - re_in * ph_i;
        float complex bb = yr + I * yi;

        /* 2. Advance carrier phasor */
        float nr = ph_r * step_r - ph_i * step_i;
        float ni = ph_r * step_i + ph_i * step_r;
        if ((s & 1023u) == 0u) {
            float inv = 1.0f / sqrtf(nr * nr + ni * ni);
            nr *= inv; ni *= inv;
        }
        ph_r = nr; ph_i = ni;

        /* 3. Code NCO — fractional chip position */
        code_phase += code_freq;
        int cur_chip = (int)code_phase;
        int cur_peak = (int)(code_phase + 0.5f);

        /* 4. EPL accumulate at EVERY sample.
         *    Early/prompt/late refer to the PRN chip at code_phase ± d.
         *    Between chip boundaries the PRN sign is constant, so this
         *    integrates baseband energy over the full chip period. */
        if (cur_chip >= 0 && cur_chip < DESPREAD_PRN_LEN) {
            int chip_e = (int)(code_phase + epl_d);
            int chip_l = (int)(code_phase - epl_d);

            float se_i = prn_sign(trk->prn_i, chip_e);
            float sp_i = prn_sign(trk->prn_i, cur_chip);
            float sl_i = prn_sign(trk->prn_i, chip_l);
            float se_q = prn_sign(trk->prn_q, chip_e);
            float sp_q = prn_sign(trk->prn_q, cur_chip);
            float sl_q = prn_sign(trk->prn_q, chip_l);

            trk->accum.early_i  += bb * se_i;
            trk->accum.prompt_i += bb * sp_i;
            trk->accum.late_i   += bb * sl_i;
            trk->accum.early_q  += bb * se_q;
            trk->accum.prompt_q += bb * sp_q;
            trk->accum.late_q   += bb * sl_q;
        }

        /* 5. Half-sine peak crossing — emit output */
        if (cur_peak != prev_peak && cur_chip >= 0 && cur_chip < DESPREAD_PRN_LEN) {
            chips_out[chip_out_idx++] = bb;
            trk->accum.n_chips++;

            /* 6. Epoch boundary — run discriminators + loop filters */
            if (trk->accum.n_chips >= trk->coh_chips) {
                process_epoch(trk);
                code_freq = trk->code_freq;
                carr_dphi = trk->carrier_freq;
                step_r = cosf(carr_dphi);
                step_i = sinf(carr_dphi);
                memset(&trk->accum, 0, sizeof(trk->accum));
            }

            prev_peak = cur_peak;
        }
    }

    trk->carrier_phase = atan2f(ph_i, ph_r);
    trk->code_phase = code_phase;

    *n_chips_out = chip_out_idx;

    float final_freq_hz = trk->carrier_freq * trk->fs / (2.0f * (float)M_PI);
    fprintf(stderr,
            "[tracking] FLL+DLL: %zu chips, %d epochs, "
            "freq=%.1f Hz, code_freq=%.6f (nominal=%.6f)\n",
            chip_out_idx, trk->epoch_count,
            (double)final_freq_hz,
            (double)trk->code_freq, 1.0 / (double)trk->sps);

    if (trk->dbg_freq && trk->dbg_len > 0) {
        FILE *f = fopen("/tmp/c_tracking_freq.bin", "wb");
        if (f) {
            fwrite(trk->dbg_freq, sizeof(float), (size_t)trk->dbg_len, f);
            fclose(f);
        }
    }

    return 0;
}

void tracking_free(tracking_state_t *trk)
{
    if (!trk) return;
    free(trk->prn_i);
    free(trk->prn_q);
    free(trk->dbg_freq);
    free(trk->dbg_phase);
    free(trk->dbg_code);
    free(trk->dbg_cn0);
    trk->prn_i = trk->prn_q = NULL;
    trk->dbg_freq = trk->dbg_phase = trk->dbg_code = trk->dbg_cn0 = NULL;
}
