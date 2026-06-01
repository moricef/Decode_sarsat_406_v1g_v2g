/**
 * @file tracking.h
 * @brief FLL + PLL + DLL burst tracking loop for DSSS/OQPSK.
 *
 * Implements the adaptive tracking architecture from Zhang et al. (ICEE 2025):
 *   - Carrier FLL (cross-product discriminator) for frequency convergence
 *   - Carrier PLL (atan2 discriminator) for phase lock
 *   - Code DLL (Early-Prompt-Late, normalized E-L) for chip timing
 *   - Kalman filter (5 states) for joint carrier/code estimation
 *   - Adaptive Switching Control: ACQ → LOCK1 → LOCK2
 *
 * The tracking loop operates at sample rate. It replaces the fixed-phase
 * decimation and Costas bypass in the previous receive chain.
 *
 * Input:  sample-rate complex samples (post-OQPSK delay, post-DC block).
 * Output: chip-rate complex samples suitable for despread_burst().
 */

#ifndef TRACKING_H
#define TRACKING_H

#include <stddef.h>
#include <stdint.h>
#include <complex.h>

/* ATC loop states */
typedef enum {
    TRK_STATE_ACQ,      /* convergence: wide BW, FLL active */
    TRK_STATE_LOCK1,    /* narrowing: FLL+PLL, Kalman activates */
    TRK_STATE_LOCK2     /* locked: PLL only, Kalman stabilized */
} trk_state_t;

/* EPL correlator accumulators for one integration epoch */
typedef struct {
    float complex early_i;
    float complex prompt_i;
    float complex late_i;
    float complex early_q;
    float complex prompt_q;
    float complex late_q;
    int n_chips;
} epl_accum_t;

#define TRK_PROMPT_HIST_LEN 20

/* Main tracking state */
typedef struct {
    /* System parameters */
    float fs;
    float sps;
    float chip_rate;

    /* Carrier NCO */
    float carrier_phase;       /* rad */
    float carrier_freq;        /* rad/sample */

    /* Code NCO */
    float code_phase;          /* fractional chip position in sample stream */
    float code_freq;           /* chips/sample (nominal = 1/sps) */
    float epl_spacing;         /* half-chip spacing (0.5 typical) */

    /* Carrier loop filter (2nd order) */
    float carr_integrator;
    float carr_alpha;
    float carr_beta;

    /* Code loop filter (1st order) */
    float code_integrator;
    float code_alpha;
    float code_beta;

    /* ATC state machine */
    trk_state_t state;
    int epoch_count;
    int lock_counter;
    int unlock_counter;
    int coh_chips;             /* 64 in ACQ, 256 in LOCK */

    /* EPL accumulators */
    epl_accum_t accum;
    float complex prev_prompt; /* for FLL cross-product (epoch k-1) */

    /* PRN sequences (pre-generated, 38400 chips each) */
    int8_t *prn_i;
    int8_t *prn_q;

    /* Lock detector */
    float cn0_est;
    float lock_indicator;      /* NBP / WBP ratio */
    float complex prompt_hist[TRK_PROMPT_HIST_LEN];
    int prompt_hist_idx;

    /* Pending corrections (applied in tracking_run after each epoch) */
    float pending_phase_correction;  /* rad */
    float pending_code_correction;   /* chips */

    /* Kalman filter (NULL until LOCK1) */
    void *kalman;

    /* Debug output */
    float *dbg_freq;
    float *dbg_phase;
    float *dbg_code;
    float *dbg_cn0;
    int dbg_len;
    int dbg_cap;
} tracking_state_t;

/**
 * @brief Initialize tracking state from coarse acquisition results.
 *
 * @param trk             State to initialize.
 * @param fs              Sample rate (Hz).
 * @param sps             Samples per chip.
 * @param coarse_freq_hz  Residual frequency offset after coarse FFT correction (Hz).
 * @param init_code_phase Initial decimation phase in samples (e.g. SPS/2).
 * @return 0 on success, -1 on error.
 */
int tracking_init(tracking_state_t *trk,
                  float fs, float sps,
                  float coarse_freq_hz,
                  float init_code_phase);

/**
 * @brief Run the tracking loop on sample-rate data.
 *
 * Processes the full burst. Produces chip-rate output for despread_burst().
 *
 * @param trk          Initialized tracking state.
 * @param samples      Input samples at sample rate.
 * @param n_samples    Number of input samples.
 * @param chips_out    Output chip-rate complex samples.
 * @param n_chips_out  [out] Number of chips produced.
 * @return 0 on success, -1 on tracking failure.
 */
int tracking_run(tracking_state_t *trk,
                 const float complex *samples,
                 size_t n_samples,
                 float complex *chips_out,
                 size_t *n_chips_out);

/**
 * @brief Free tracking state resources (PRN buffers, Kalman, debug).
 */
void tracking_free(tracking_state_t *trk);

#endif /* TRACKING_H */
