/**
 * @file tracking.c
 * @brief FLL + PLL + DLL burst tracking loop for DSSS/OQPSK.
 *
 * Phase 3: PLL (Costas discriminator) + ATC (Adaptive Switching Control).
 *          - ACQ:  FLL only, coh=64,  BW=10 Hz
 *          - LOCK1: FLL+PLL, coh=128, BW=5 Hz
 *          - LOCK2: PLL only, coh=256, BW=2 Hz
 *          - Lock detector: NBP/WBP over 20-epoch window
 *          - Phase correction applied to carrier phasor each epoch
 *
 * Architecture per Zhang et al. (ICEE 2025), Fig. 59.1.
 */

#include "tracking.h"
#include "kalman5.h"
#include "despread.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Loop bandwidths per ATC state (Hz) */
#define DLL_BW_ACQ    1.0f
#define DLL_BW_LOCK   0.5f
#define FLL_BW_ACQ    2.0f
#define FLL_BW_LOCK1  1.5f
#define PLL_BW_LOCK1  5.0f
#define PLL_BW_LOCK2  2.0f

/* ATC thresholds */
#define LOCK1_THRESH   0.5f
#define LOCK2_THRESH   0.8f
#define UNLOCK_THRESH  0.3f
#define LOCK1_COUNT    10
#define LOCK2_COUNT    20
#define UNLOCK_COUNT    5
#define ATC_HOLD_COUNT  5   /* epochs to suspend ATC after state change */

/* ------------------------------------------------------------------ */
/* Compute 1st-order code loop filter gains.                          */
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
/*   d_freq = atan2(cross, dot) / (2*pi*T)   [Hz]                     */
/* ------------------------------------------------------------------ */
static float fll_discriminator(float complex prev, float complex cur, float T)
{
    float cross = crealf(prev) * cimagf(cur) - cimagf(prev) * crealf(cur);
    float dot   = crealf(prev) * crealf(cur) + cimagf(prev) * cimagf(cur);
    return atan2f(cross, dot) / (2.0f * (float)M_PI * T);
}

/* ------------------------------------------------------------------ */
/* PLL discriminator: Costas for QPSK/OQPSK, data-insensitive.         */
/*   d_phase = sign(Re(P))*Im(P) - sign(Im(P))*Re(P)                  */
/*   Normalised by |P|² for constant gain vs amplitude.                */
/* ------------------------------------------------------------------ */
static float pll_discriminator(float complex prompt)
{
    float re = crealf(prompt);
    float im = cimagf(prompt);
    float dot = re * re + im * im;
    if (dot < 1e-20f) return 0.0f;

    /* QPSK Costas: sign(I)*Q - sign(Q)*I */
    float d_phase = (re > 0.0f ? im : -im) - (im > 0.0f ? re : -re);
    return d_phase / dot;   /* normalised → ~phase error in rad */
}

/* ------------------------------------------------------------------ */
/* Lock detector: NBP/WBP ratio over prompt_hist window.               */
/*   NBP = |Σ P[k]|²   (coherent),  WBP = Σ |P[k]|²                   */
/*   lock_indicator = 1.0 when perfectly locked, ~1/N when unlocked.   */
/* ------------------------------------------------------------------ */
static void update_lock_detector(tracking_state_t *trk)
{
    /* Store current prompt in circular history */
    trk->prompt_hist[trk->prompt_hist_idx] = trk->accum.prompt_i;
    trk->prompt_hist_idx = (trk->prompt_hist_idx + 1) % TRK_PROMPT_HIST_LEN;

    /* Compute NBP / WBP every epoch for fast ATC response */
    if (trk->epoch_count > 0) {
        float complex sum = 0.0f;
        float sum_pow = 0.0f;
        int n_valid = 0;
        for (int i = 0; i < TRK_PROMPT_HIST_LEN; i++) {
            float p = crealf(trk->prompt_hist[i]);
            float q = cimagf(trk->prompt_hist[i]);
            if (p == 0.0f && q == 0.0f) continue;  /* skip unfilled slots */
            sum += trk->prompt_hist[i];
            sum_pow += p * p + q * q;
            n_valid++;
        }
        if (n_valid > 1 && sum_pow > 1e-20f) {
            float nbp = crealf(sum) * crealf(sum) + cimagf(sum) * cimagf(sum);
            trk->lock_indicator = nbp / (sum_pow * (float)n_valid);
        } else {
            trk->lock_indicator = 0.0f;
        }
    }
}

/* ------------------------------------------------------------------ */
/* ATC: check transitions and apply new state parameters.              */
/* ------------------------------------------------------------------ */
static void atc_update(tracking_state_t *trk)
{
    float li = trk->lock_indicator;

    switch (trk->state) {

    case TRK_STATE_ACQ:
        if (li > LOCK1_THRESH) {
            trk->lock_counter++;
            if (trk->lock_counter >= LOCK1_COUNT) {
                trk->state = TRK_STATE_LOCK1;
                trk->coh_chips = 128;
                trk->lock_counter = -ATC_HOLD_COUNT;  /* hold after transition */
                trk->unlock_counter = 0;
                compute_carrier_gains(trk, FLL_BW_LOCK1);
                compute_code_gains(trk, DLL_BW_LOCK);
                if (!trk->kalman) {
                    trk->kalman = malloc(sizeof(kalman5_t));
                    if (trk->kalman) {
                        float Tk = (float)trk->coh_chips / trk->chip_rate;
                        kalman5_init((kalman5_t *)trk->kalman, Tk, 406.0e6f, trk->chip_rate);
                    }
                }
                fprintf(stderr, "[tracking] ATC: ACQ → LOCK1 (coh=%d, lock=%.3f)\n",
                        trk->coh_chips, (double)li);
            }
        } else {
            if (trk->lock_counter >= 0) trk->lock_counter = 0;
        }
        break;

    case TRK_STATE_LOCK1:
        if (li > LOCK2_THRESH && trk->lock_counter >= 0) {
            trk->lock_counter++;
            if (trk->lock_counter >= LOCK2_COUNT) {
                trk->state = TRK_STATE_LOCK2;
                trk->coh_chips = 256;
                trk->lock_counter = -ATC_HOLD_COUNT;
                trk->unlock_counter = 0;
                compute_carrier_gains(trk, PLL_BW_LOCK2);
                compute_code_gains(trk, DLL_BW_LOCK);
                fprintf(stderr, "[tracking] ATC: LOCK1 → LOCK2 (coh=%d, lock=%.3f)\n",
                        trk->coh_chips, (double)li);
            }
        } else if (li <= LOCK2_THRESH && trk->lock_counter >= 0) {
            trk->lock_counter = 0;
        }
        /* hold-off: negative counter, just increment toward 0 */
        if (trk->lock_counter < 0) trk->lock_counter++;

        if (li < UNLOCK_THRESH && trk->unlock_counter >= 0) {
            trk->unlock_counter++;
            if (trk->unlock_counter >= UNLOCK_COUNT) {
                trk->state = TRK_STATE_ACQ;
                trk->coh_chips = 64;
                trk->lock_counter = -ATC_HOLD_COUNT;
                trk->unlock_counter = 0;
                trk->carr_integrator = 0.0f;
                compute_carrier_gains(trk, FLL_BW_ACQ);
                compute_code_gains(trk, DLL_BW_ACQ);
                free(trk->kalman);
                trk->kalman = NULL;
                fprintf(stderr, "[tracking] ATC: LOCK1 → ACQ (lost lock, lock=%.3f)\n",
                        (double)li);
            }
        } else if (li >= UNLOCK_THRESH && trk->unlock_counter >= 0) {
            trk->unlock_counter = 0;
        }
        if (trk->unlock_counter < 0) trk->unlock_counter++;
        break;

    case TRK_STATE_LOCK2:
        if (li < UNLOCK_THRESH && trk->unlock_counter >= 0) {
            trk->unlock_counter++;
            if (trk->unlock_counter >= UNLOCK_COUNT) {
                trk->state = TRK_STATE_LOCK1;
                trk->coh_chips = 128;
                trk->lock_counter = -ATC_HOLD_COUNT;
                trk->unlock_counter = 0;
                compute_carrier_gains(trk, FLL_BW_LOCK1);
                compute_code_gains(trk, DLL_BW_LOCK);
                fprintf(stderr, "[tracking] ATC: LOCK2 → LOCK1 (coh=%d, lock=%.3f)\n",
                        trk->coh_chips, (double)li);
            }
        } else if (li >= UNLOCK_THRESH && trk->unlock_counter >= 0) {
            trk->unlock_counter = 0;
        }
        if (trk->unlock_counter < 0) trk->unlock_counter++;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Process one completed epoch: discriminators + loop filters + ATC.    */
/* ------------------------------------------------------------------ */
static void process_epoch(tracking_state_t *trk)
{
    float T = (float)trk->coh_chips / trk->chip_rate;

    /* --- DLL (always active) --- */
    float d_tau = dll_discriminator(&trk->accum);
    trk->code_freq += trk->code_alpha * d_tau / trk->sps;

    float nominal = 1.0f / trk->sps;
    float max_dev = nominal * 0.01f;
    if (trk->code_freq > nominal + max_dev) trk->code_freq = nominal + max_dev;
    if (trk->code_freq < nominal - max_dev) trk->code_freq = nominal - max_dev;

    /* --- FLL (cross-product, ACQ + LOCK1) --- */
    float d_freq = 0.0f;
    int fll_active = (trk->state == TRK_STATE_ACQ || trk->state == TRK_STATE_LOCK1);
    if (fll_active) {
        float dot_prev = crealf(trk->prev_prompt) * crealf(trk->prev_prompt)
                       + cimagf(trk->prev_prompt) * cimagf(trk->prev_prompt);
        if (dot_prev > 1e-12f) {
            d_freq = fll_discriminator(trk->prev_prompt, trk->accum.prompt_i, T);
            trk->carr_integrator += trk->carr_beta * d_freq;
        }
    }

    /* --- PLL (Costas, LOCK1 + LOCK2) --- */
    float d_phase = 0.0f;
    int pll_active = (trk->state == TRK_STATE_LOCK1 || trk->state == TRK_STATE_LOCK2);
    if (pll_active) {
        d_phase = pll_discriminator(trk->accum.prompt_i);

        if (0 && trk->state == TRK_STATE_LOCK2) {
            /* PLL-only integrator — disabled pending PLL noise fix */
            trk->carr_integrator += trk->carr_beta * d_phase;
        }

        /* Phase correction for phasor (proportional term)
         * FIXME: disabled until Q-cross-talk PLL noise is resolved */
        (void)d_phase;
        trk->pending_phase_correction = 0.0f;
    } else {
        trk->pending_phase_correction = 0.0f;
    }

    /* --- Carrier NCO update --- */
    float freq_hz = trk->carr_integrator;
    if (fll_active)
        freq_hz += trk->carr_alpha * d_freq;
    trk->carrier_freq = 2.0f * (float)M_PI * freq_hz / trk->fs;

    /* --- Kalman joint estimation (LOCK1/LOCK2) --- */
    if (trk->kalman) {
        kalman5_t *kf = (kalman5_t *)trk->kalman;
        kf->T = T;

        /* Safeguard: bypass Kalman if d_freq is noise (variance > 400 Hz²
         * over recent history), indicating no real signal to track.
         * Prevents Kalman from injecting noise on already-correct data. */
        static float df_hist[8], df_sq_hist[8];
        static int df_hist_idx = 0, df_hist_fill = 0;
        df_hist[df_hist_idx & 7] = d_freq;
        df_sq_hist[df_hist_idx & 7] = d_freq * d_freq;
        df_hist_idx++;
        if (df_hist_fill < 8) df_hist_fill++;

        float df_mean = 0.0f, df_var = 0.0f;
        for (int i = 0; i < df_hist_fill; i++) {
            df_mean += df_hist[i];
            df_var  += df_sq_hist[i];
        }
        df_mean /= (float)df_hist_fill;
        df_var  = df_var / (float)df_hist_fill - df_mean * df_mean;

        if (df_var < 400.0f) {   /* skip Kalman when d_freq is noise */
            kalman5_predict(kf);
            float z[KF_M];
            z[0] = d_phase;
            z[1] = d_freq;
            z[2] = d_tau;
            kalman5_update(kf, z);
            if ((trk->epoch_count % 20) == 0)
                kalman5_adapt_noise(kf);
            trk->pending_phase_correction += kf->x[0];
            float kf_hz = kf->x[1];
            trk->carrier_freq += 2.0f * (float)M_PI * kf_hz / trk->fs;
            trk->pending_code_correction += kf->x[3];
            trk->code_freq += kf->x[4] / trk->chip_rate;
        }
    }

    /* --- Lock detector + ATC --- */
    update_lock_detector(trk);
    atc_update(trk);

    /* --- Housekeeping --- */
    trk->prev_prompt = trk->accum.prompt_i;

    /* Debug trace */
    if (trk->dbg_freq && trk->dbg_len < trk->dbg_cap) {
        trk->dbg_freq[trk->dbg_len]  = freq_hz;
        trk->dbg_phase[trk->dbg_len] = trk->pending_phase_correction;
        trk->dbg_code[trk->dbg_len]  = d_tau;
        trk->dbg_cn0[trk->dbg_len]   = trk->lock_indicator;
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
    trk->pending_phase_correction = 0.0f;

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

        /* 4. EPL accumulate at EVERY sample. */
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

                /* Update carrier NCO */
                carr_dphi = trk->carrier_freq;
                step_r = cosf(carr_dphi);
                step_i = sinf(carr_dphi);

                /* Apply phase correction to phasor */
                if (trk->pending_phase_correction != 0.0f) {
                    float dp = trk->pending_phase_correction;
                    float cos_dp = cosf(dp), sin_dp = sinf(dp);
                    float pr = ph_r * cos_dp + ph_i * sin_dp;
                    float pi = ph_i * cos_dp - ph_r * sin_dp;
                    ph_r = pr; ph_i = pi;
                    trk->pending_phase_correction = 0.0f;
                }
                /* Apply code phase correction (Kalman) */
                if (trk->pending_code_correction != 0.0f) {
                    code_phase += trk->pending_code_correction;
                    trk->pending_code_correction = 0.0f;
                }

                memset(&trk->accum, 0, sizeof(trk->accum));
            }

            prev_peak = cur_peak;
        }
    }

    trk->carrier_phase = atan2f(ph_i, ph_r);
    trk->code_phase = code_phase;

    *n_chips_out = chip_out_idx;

    float final_freq_hz = trk->carrier_freq * trk->fs / (2.0f * (float)M_PI);
    const char *state_name =
        trk->state == TRK_STATE_ACQ  ? "ACQ" :
        trk->state == TRK_STATE_LOCK1 ? "LOCK1" : "LOCK2";
    fprintf(stderr,
            "[tracking] %s: %zu chips, %d epochs, "
            "freq=%.1f Hz, lock=%.3f, coh=%d\n",
            state_name, chip_out_idx, trk->epoch_count,
            (double)final_freq_hz,
            (double)trk->lock_indicator, trk->coh_chips);

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
    free(trk->kalman);
    trk->prn_i = trk->prn_q = NULL;
    trk->dbg_freq = trk->dbg_phase = trk->dbg_code = trk->dbg_cn0 = NULL;
    trk->kalman = NULL;
}
