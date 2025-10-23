/**
 * @file dsss_demod.c
 * @brief DSSS/OQPSK Demodulator for COSPAS-SARSAT 2G Beacons (T.018)
 *
 * Complete receiver implementation based on:
 * - MATLAB DSSSReceiverForSARbasedTrackingSystem.pdf (MathWorks R2024a)
 * - C/S T.018 Rev.12 Specification
 *
 * ⚠️  WARNING: THIS IMPLEMENTATION IS NOT FUNCTIONAL ⚠️
 *
 * Current Status: PAUSED - 55.3% bit accuracy (need >95%)
 * Date: 2025-10-19
 *
 * KNOWN BUGS (Documented in ETAT_PAUSE_DEMODULATEUR.md):
 *
 * 1. TIMING RECOVERY (lines 333-387):
 *    - Only 33,079 symbols recovered vs 38,400 expected
 *    - Signal appears shifted by -7 bits
 *    - SNR very low (1.1 dB on perfect signal, should be >20 dB)
 *    - Gardner TED may be biased or incorrectly implemented
 *    - Loop filter gains may be wrong
 *
 * 2. PHASE AMBIGUITY RESOLUTION (lines 393-460):
 *    - Tests phase on SPREAD symbols (INCORRECT!)
 *    - Should test AFTER despreading on preamble
 *    - Current result: 90° rotation with 48.6% correlation (random)
 *
 * 3. DSSS DESPREADING (lines 466-550):
 *    - Mean correlation = 0.053 (should be >0.7)
 *    - Indicates PRN chips not synchronized with received chips
 *    - May have timing offset, chip convention error, or PRN misalignment
 *
 * 4. COSTAS LOOP (lines 629-648):
 *    - DISABLED: Was diverging instead of converging
 *    - Estimated +10.6 kHz offset on signal at -0.164 kHz
 *    - Loop bandwidth or phase detector may be incorrect for DSSS
 *
 * TEST RESULTS on test_known.iq (known frame, perfect SNR):
 *   Expected: 89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
 *   Obtained: B8D0EE0EF7AB69EB41B70F0DD30C7E1554151344C05CAB652A43418FAD68757A...
 *   Match: 55.3% (no simple transformation improves this)
 *
 * See ETAT_PAUSE_DEMODULATEUR.md for:
 * - Complete bug analysis
 * - Corrections attempted
 * - Options for future resumption
 *
 * @author Collaborative development (2025)
 * @license Creative Commons CC BY-NC-SA
 */

#include "dsss_demod.h"
#include "prn_generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Saturate complex signal to prevent overflow
 */
static void saturate_complex(float complex *signal, size_t len, float abs_limit) {
    for (size_t i = 0; i < len; i++) {
        float mag = cabsf(signal[i]);
        if (mag > abs_limit) {
            signal[i] = signal[i] / mag * abs_limit;
        }
    }
}

/**
 * @brief Convert OQPSK to QPSK by removing Q channel delay
 */
static void oqpsk_to_qpsk(const float complex *oqpsk, float complex *qpsk,
                          size_t num_samples, int sps) {
    int delay = sps / 2;
    for (size_t i = 0; i < num_samples - delay; i++) {
        qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[i + delay]);
    }
}

/**
 * @brief Estimate SNR from signal
 */
static float estimate_snr(const float complex *signal, size_t len) {
    // Calculate signal power
    float sig_power = 0.0f;
    for (size_t i = 0; i < len; i++) {
        sig_power += cabsf(signal[i]) * cabsf(signal[i]);
    }
    sig_power /= len;

    // Estimate noise from constellation scatter
    float noise_power = 0.0f;
    for (size_t i = 0; i < len; i++) {
        float complex ideal = (crealf(signal[i]) > 0 ? 1.0f : -1.0f) +
                              I * (cimagf(signal[i]) > 0 ? 1.0f : -1.0f);
        float error = cabsf(signal[i] - ideal);
        noise_power += error * error;
    }
    noise_power /= len;

    if (noise_power > 0) {
        return 10.0f * log10f(sig_power / noise_power);
    }
    return 60.0f; // Very high SNR
}

// =============================================================================
// AGC (Automatic Gain Control)
// =============================================================================

int dsss_agc(const float complex *input, float complex *output,
             size_t num_samples, float *gain_out) {
    const float adapt_rate = 0.01f;
    const float max_gain_db = 60.0f;
    const float max_gain = powf(10.0f, max_gain_db / 20.0f);

    float gain = 1.0f;
    float power_est = 0.0f;

    for (size_t i = 0; i < num_samples; i++) {
        // Update power estimate
        float sample_power = cabsf(input[i]) * cabsf(input[i]);
        power_est = (1.0f - adapt_rate) * power_est + adapt_rate * sample_power;

        // Update gain to maintain unit power
        if (power_est > 0) {
            float target_gain = 1.0f / sqrtf(power_est);
            gain = fminf(target_gain, max_gain);
        }

        // Apply gain
        output[i] = gain * input[i];
    }

    // Saturate to prevent overflow
    saturate_complex(output, num_samples, 1.2f);

    if (gain_out) *gain_out = gain;
    return 0;
}

// =============================================================================
// PREAMBLE DETECTION
// =============================================================================

/**
 * @brief Generate preamble reference sequence
 */
static int generate_preamble_reference(float complex *preamble, int length) {
    // Generate PRN for preamble
    prn_state_t prn_state;
    prn_init(&prn_state, 0);  // Normal mode

    int8_t prn_i_buf[DSSS_SPREADING_FACTOR];
    int8_t prn_q_buf[DSSS_SPREADING_FACTOR];

    int chip_idx = 0;
    for (int bit = 0; bit < length / (DSSS_SPREADING_FACTOR / 2); bit++) {
        prn_generate_i(&prn_state, prn_i_buf);
        prn_generate_q(&prn_state, prn_q_buf);

        // Preamble is alternating 0,1 pattern
        // For simplicity, use first half of chips for preamble detection
        for (int c = 0; c < DSSS_SPREADING_FACTOR / 2 && chip_idx < length; c++) {
            float i_val = (bit % 2 == 0) ? (float)prn_i_buf[c] : -(float)prn_i_buf[c];
            float q_val = (bit % 2 == 0) ? (float)prn_q_buf[c] : -(float)prn_q_buf[c];
            preamble[chip_idx++] = (i_val * 2.0f - 1.0f) + I * (q_val * 2.0f - 1.0f);
        }
    }

    return 0;
}

/**
 * @brief Correlate two signals
 */
static float correlate_signals(const float complex *sig1, const float complex *sig2,
                               size_t len) {
    float complex corr = 0.0f;
    for (size_t i = 0; i < len; i++) {
        corr += sig1[i] * conjf(sig2[i]);
    }
    return cabsf(corr) / len;
}

int dsss_detect_preamble(const float complex *samples, size_t num_samples,
                         float samp_rate, int *preamble_idx,
                         float *freq_offset, float *correlation) {
    // Preamble detection parameters
    const int preamble_offset = 200;  // Skip AGC settling
    const int preamble_length = 500;   // Longer correlation for better detection

    // Generate reference preamble
    float complex *preamble_ref = calloc(preamble_length, sizeof(float complex));
    if (!preamble_ref) return -1;

    generate_preamble_reference(preamble_ref, preamble_length);

    // Convert OQPSK to QPSK for detection
    int sps = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);
    float complex *samples_qpsk = calloc(num_samples, sizeof(float complex));
    if (!samples_qpsk) {
        free(preamble_ref);
        return -1;
    }

    oqpsk_to_qpsk(samples, samples_qpsk, num_samples, sps);

    // Search over frequency offsets (±12 kHz for LEO Doppler)
    float best_corr = 0.0f;
    int best_idx = -1;
    float best_freq = 0.0f;

    // Enable debug output for first 10 frequencies
    int debug_count = 0;
    const int debug_max = 10;

    printf("  Frequency search (testing every %d Hz, range ±%d Hz):\n",
           DSSS_FREQ_SEARCH_STEP, DSSS_MAX_DOPPLER);

    for (float f_offset = -DSSS_MAX_DOPPLER;
         f_offset <= DSSS_MAX_DOPPLER;
         f_offset += DSSS_FREQ_SEARCH_STEP) {

        // Apply frequency offset to reference
        float complex *preamble_shifted = calloc(preamble_length, sizeof(float complex));
        if (!preamble_shifted) continue;

        for (int i = 0; i < preamble_length; i++) {
            float t = (float)i / samp_rate;
            preamble_shifted[i] = preamble_ref[i] * cexpf(I * 2.0f * M_PI * f_offset * t);
        }

        // Correlate with received signal (search first 20% of buffer where preamble should be)
        size_t search_length = num_samples / 5;  // Search first 20%
        float max_corr_this_freq = 0.0f;
        int max_idx_this_freq = -1;

        for (size_t idx = 0; idx < search_length - preamble_length; idx += sps) {
            float corr = correlate_signals(&samples_qpsk[idx],
                                          preamble_shifted, preamble_length);

            if (corr > max_corr_this_freq) {
                max_corr_this_freq = corr;
                max_idx_this_freq = idx;
            }
        }

        // Update global best
        if (max_corr_this_freq > best_corr) {
            best_corr = max_corr_this_freq;
            best_idx = max_idx_this_freq;
            best_freq = f_offset;
        }

        // Debug output for interesting frequencies
        if (debug_count < debug_max ||
            fabs(f_offset) < 500 ||  // Around 0 Hz
            max_corr_this_freq > 0.7) {  // High correlation
            printf("    f=%+7.0f Hz: corr=%.3f at idx=%d%s\n",
                   f_offset, max_corr_this_freq, max_idx_this_freq,
                   (max_corr_this_freq == best_corr) ? " ← BEST" : "");
            debug_count++;
        }

        free(preamble_shifted);

        // Disabled early exit to find true peak
        // (Re-enable after debugging)
        // if (best_corr > 0.95f) {
        //     printf("    (Early exit: excellent correlation %.3f)\n", best_corr);
        //     break;
        // }
    }

    printf("  Completed frequency search: tested %d frequencies\n",
           (int)((2 * DSSS_MAX_DOPPLER) / DSSS_FREQ_SEARCH_STEP) + 1);

    free(preamble_ref);
    free(samples_qpsk);

    if (best_corr < 0.3f) {
        fprintf(stderr, "Preamble not detected (best corr: %.3f)\n", best_corr);
        return -1;
    }

    printf("Found preamble at index %d, freq offset %.1f Hz, corr %.3f\n",
           best_idx, best_freq, best_corr);

    if (preamble_idx) *preamble_idx = best_idx;
    if (freq_offset) *freq_offset = best_freq;
    if (correlation) *correlation = best_corr;

    return 0;
}

// =============================================================================
// FREQUENCY CORRECTION
// =============================================================================

// Helper: sign function for complex
static float complex csignf(float complex z) {
    float r = crealf(z) >= 0 ? 1.0f : -1.0f;
    float i = cimagf(z) >= 0 ? 1.0f : -1.0f;
    return r + I * i;
}

void dsss_frequency_correction(const float complex *input, float complex *output,
                               size_t num_samples, float freq_offset, float samp_rate) {
    for (size_t i = 0; i < num_samples; i++) {
        float t = (float)i / samp_rate;
        output[i] = input[i] * cexpf(-I * 2.0f * M_PI * freq_offset * t);
    }
}

int dsss_fine_frequency_sync(const float complex *input, float complex *output,
                             size_t num_samples, float samp_rate, float *freq_est) {
    // Costas loop parameters
    const float loop_bw = 0.01f;
    const float damping = 0.707f;
    const int sps = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);

    // PLL gains
    float k1 = 4.0f * damping * loop_bw / (1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw);
    float k2 = 4.0f * loop_bw * loop_bw / (1.0f + 2.0f * damping * loop_bw + loop_bw * loop_bw);

    float phase_est = 0.0f;
    float freq_est_local = 0.0f;

    for (size_t i = 0; i < num_samples; i++) {
        // Apply current phase estimate
        output[i] = input[i] * cexpf(-I * phase_est);

        // Phase error detection (at symbol sampling points)
        float phase_err = 0.0f;
        if (i % sps == 0) {
            float complex sym = output[i];
            // Simple phase error detector for QPSK
            phase_err = cimagf(sym * conjf(csignf(sym)));
        }

        // Update PLL
        freq_est_local += k2 * phase_err;
        phase_est += freq_est_local + k1 * phase_err;

        // Wrap phase
        while (phase_est > M_PI) phase_est -= 2.0f * M_PI;
        while (phase_est < -M_PI) phase_est += 2.0f * M_PI;
    }

    if (freq_est) {
        *freq_est = freq_est_local * samp_rate / (2.0f * M_PI);
    }

    return 0;
}

// =============================================================================
// TIMING RECOVERY HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Calculate loop filter gains for timing recovery or PLL
 *
 * Uses standard second-order loop filter equations:
 *   k1 = (4 * zeta * omega_n) / (1 + 2*zeta*omega_n + omega_n^2)
 *   k2 = (4 * omega_n^2) / (1 + 2*zeta*omega_n + omega_n^2)
 *
 * where omega_n = loop_bw (normalized loop bandwidth)
 *       zeta = damping factor
 *
 * @param loop_bw Normalized loop bandwidth (typical: 0.001 - 0.01)
 * @param damping Damping factor (typical: 0.707 for critical, 1.0-2.0 for overdamped)
 * @param k1 Output: proportional gain
 * @param k2 Output: integral gain
 */
static void calculate_loop_gains(float loop_bw, float damping, float *k1, float *k2) {
    float zeta = damping;
    float omega_n = loop_bw;
    float denom = 1.0f + 2.0f * zeta * omega_n + omega_n * omega_n;

    *k1 = (4.0f * zeta * omega_n) / denom;
    *k2 = (4.0f * omega_n * omega_n) / denom;
}

/**
 * @brief Cubic interpolation for sample extraction
 *
 * Uses 4-point cubic (Catmull-Rom) interpolation to extract a sample
 * at fractional position mu between samples.
 *
 * Formula: y(mu) = a0*mu^3 + a1*mu^2 + a2*mu + a3
 * where:
 *   a0 = -0.5*y[-1] + 1.5*y[0] - 1.5*y[1] + 0.5*y[2]
 *   a1 =      y[-1] - 2.5*y[0] + 2.0*y[1] - 0.5*y[2]
 *   a2 = -0.5*y[-1]            + 0.5*y[1]
 *   a3 =                 y[0]
 *
 * @param samples Input sample array
 * @param mu Fractional position (0.0 to 1.0) between samples[idx] and samples[idx+1]
 * @param idx Center index (must have idx-1 and idx+2 available)
 * @param len Total array length (for bounds checking)
 * @return Interpolated complex sample at position idx + mu
 */
static float complex interpolate_cubic(const float complex *samples, float mu, int idx, int len) {
    // Get 4 surrounding samples with bounds checking
    int idx0 = idx - 1;
    int idx1 = idx;
    int idx2 = idx + 1;
    int idx3 = idx + 2;

    // Clamp indices to valid range
    if (idx0 < 0) idx0 = 0;
    if (idx3 >= len) idx3 = len - 1;

    float complex y0 = samples[idx0];
    float complex y1 = samples[idx1];
    float complex y2 = samples[idx2];
    float complex y3 = samples[idx3];

    // Cubic polynomial coefficients (Catmull-Rom spline)
    float complex a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    float complex a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float complex a2 = -0.5f * y0 + 0.5f * y2;
    float complex a3 = y1;

    // Evaluate polynomial at mu
    float mu_sq = mu * mu;
    float mu_cubed = mu_sq * mu;

    return a0 * mu_cubed + a1 * mu_sq + a2 * mu + a3;
}

/**
 * @brief Gardner Timing Error Detector
 *
 * Computes timing error from 3 successive symbols:
 *   error = Re[(late - early) * conj(prompt)]
 *
 * This is a non-data-aided detector that works for BPSK/QPSK.
 *
 * @param early Symbol at n-1
 * @param prompt Symbol at n (sampled between early and late)
 * @param late Symbol at n+1
 * @return Timing error (positive = sampling too early, negative = too late)
 */
static float gardner_ted(float complex early, float complex prompt, float complex late) {
    return crealf((late - early) * conjf(prompt));
}

// =============================================================================
// TIMING RECOVERY
// =============================================================================

/**
 * ⚠️  BUG #1: TIMING RECOVERY INCORRECT ⚠️ (EN COURS DE CORRECTION)
 *
 * SYMPTOMS:
 * - Recovers only ~33k symbols instead of expected 38,400
 * - Signal appears shifted by -7 bits in output
 * - Results in very low SNR (1.1 dB) even on perfect input signal
 *
 * SUSPECTED CAUSES:
 * - Gardner TED implementation may be biased
 * - Loop filter gains (k1, k2) may be incorrect
 * - Sampling at wrong instants within symbol period
 * - Interpolation issues (currently using simple averaging)
 * - Initial timing phase may start at wrong offset
 *
 * CORRECT APPROACH SHOULD:
 * 1. Use proper interpolation (cubic, Farrow filter, etc.)
 * 2. Sample at optimal point (not average over sps)
 * 3. Validate TED output with plots
 * 4. Verify loop filter gains produce stable convergence
 * 5. Test on known timing offsets (0, 0.25T, 0.5T, 0.75T)
 *
 * REFERENCES:
 * - "Digital Communication Receivers" by Meyr, Moeneclaey, Fechtel
 * - GNU Radio Symbol Sync block implementation
 */
int dsss_timing_recovery(const float complex *input, float complex *output,
                         size_t num_samples, size_t *num_symbols, float samp_rate) {
    const float loop_bw = 0.001f;
    const float damping = 2.0f;
    const int sps = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);

    // Calculate loop filter gains using proper formula
    float k1, k2;
    calculate_loop_gains(loop_bw, damping, &k1, &k2);

    printf("[TIMING] Loop parameters: bw=%.5f, damping=%.2f, sps=%d\n", loop_bw, damping, sps);
    printf("[TIMING] Loop gains: k1=%.6f, k2=%.6f\n", k1, k2);

    // Timing state variables
    // Initialize timing_phase to allow cubic interpolation (needs idx >= 1)
    float timing_phase = 2.0f;           // Start at sample 2 (allows interpolation at idx-1)
    float timing_freq = 0.0f;            // Accumulated frequency error (NCO)
    size_t symbol_count = 0;

    // Symbol history for Gardner TED
    float complex prev_prev_sym = 0.0f;
    float complex prev_sym = 0.0f;

    // Debug: track timing error statistics
    float timing_error_sum = 0.0f;
    float timing_error_max = 0.0f;
    int debug_print_interval = 5000;     // Print every N symbols

    // Main timing recovery loop
    while (timing_phase < num_samples - sps && symbol_count < 40000) {
        // Calculate sample index with fractional part
        int center_idx = (int)floorf(timing_phase);
        float mu = timing_phase - floorf(timing_phase);  // Fractional position [0, 1)

        // Bounds check for cubic interpolation (needs idx-1 to idx+2)
        if (center_idx < 1 || center_idx + 2 >= (int)num_samples) {
            break;
        }

        // Extract symbol using cubic interpolation
        float complex symbol = interpolate_cubic(input, mu, center_idx, num_samples);

        output[symbol_count] = symbol;
        symbol_count++;

        // Compute timing error using Gardner TED
        float timing_error = 0.0f;
        if (symbol_count >= 3) {
            timing_error = gardner_ted(prev_prev_sym, prev_sym, symbol);

            // Track error statistics
            timing_error_sum += fabsf(timing_error);
            if (fabsf(timing_error) > timing_error_max) {
                timing_error_max = fabsf(timing_error);
            }
        }

        // Update timing loop filter (proportional + integral)
        timing_freq += k2 * timing_error;       // Integral path (NCO frequency)
        timing_phase += sps + k1 * timing_error + timing_freq;  // Advance to next symbol

        // Debug output every N symbols
        if (symbol_count > 0 && symbol_count % debug_print_interval == 0) {
            float avg_error = timing_error_sum / debug_print_interval;
            printf("[TIMING] Symbol %zu: phase=%.2f, mu=%.4f, error=%.4f (avg=%.4f, max=%.4f), freq=%.6f\n",
                   symbol_count, timing_phase, mu, timing_error, avg_error, timing_error_max, timing_freq);
            timing_error_sum = 0.0f;
            timing_error_max = 0.0f;
        }

        // Update symbol history
        prev_prev_sym = prev_sym;
        prev_sym = symbol;
    }

    // Final statistics
    printf("[TIMING] Recovery complete: %zu symbols recovered (expected ~38400)\n", symbol_count);
    printf("[TIMING] Final timing phase: %.2f, final NCO freq: %.6f\n", timing_phase, timing_freq);

    if (num_symbols) *num_symbols = symbol_count;
    return 0;
}

// =============================================================================
// PHASE AMBIGUITY RESOLUTION
// =============================================================================

/**
 * ⚠️  BUG #2: PHASE AMBIGUITY - CORRECTION OPTIMISÉE ⚠️
 *
 * APPROCHE CORRIGÉE (Optimisée) :
 * 1. Pour chaque combinaison (4 rotations × 2 swaps = 8 total)
 * 2. Applique transformation aux symboles
 * 3. Démodule en chips I/Q (SEULEMENT preamble = 12,800 chips)
 * 4. Désétale ces 50 bits de preamble
 * 5. Compare avec pattern attendu [0,1,0,1,...]
 * 6. Garde la meilleure combinaison (corrélation >90%)
 *
 * OPTIMISATION :
 * - Désétale seulement 50 bits × 8 combinaisons au lieu de 300 bits × 8
 * - 12,800 chips × 8 = 102,400 ops vs 76,800 × 8 = 614,400 ops
 * - Gain : 6× plus rapide
 *
 * @param symbols Input QPSK symbols (chip-rate, after timing recovery)
 * @param num_symbols Number of symbols (should be ~38,400)
 * @param phase_rot Output: best phase rotation (0-3)
 * @param iq_swap Output: best I/Q swap flag
 * @return 0 on success (>90% corr), -1 if no good match found
 */
int dsss_resolve_phase_ambiguity(const float complex *symbols, size_t num_symbols,
                                 int *phase_rot, bool *iq_swap,
                                 dsss_demod_state_t *state) {
    printf("[PHASE] Option C: Exhaustive search for optimal parameters...\n");
    printf("[PHASE] Phase 1: Testing 8 phase/swap combinations...\n");

    // Expected preamble: alternating 0,1,0,1,0,1... (50 bits)
    uint8_t expected_preamble[DSSS_PREAMBLE_LENGTH];
    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
        expected_preamble[i] = i % 2;
    }

    // Number of chips for preamble (50 bits × 256 chips/bit = 12,800 chips)
    const int preamble_chips = DSSS_PREAMBLE_LENGTH * DSSS_SPREADING_FACTOR;

    // Check we have enough symbols
    if (num_symbols < preamble_chips) {
        fprintf(stderr, "[PHASE] Error: Not enough symbols for preamble (%zu < %d)\n",
                num_symbols, preamble_chips);
        return -1;
    }

    // Generate PRN sequences for preamble (25 bits I + 25 bits Q)
    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    const int prn_bits = DSSS_PREAMBLE_LENGTH / 2;  // 25 bits per channel
    uint8_t *prn_i_full = malloc(prn_bits * DSSS_SPREADING_FACTOR);
    uint8_t *prn_q_full = malloc(prn_bits * DSSS_SPREADING_FACTOR);

    if (!prn_i_full || !prn_q_full) {
        free(prn_i_full);
        free(prn_q_full);
        return -1;
    }

    // Generate I-channel PRN (signed -1/+1)
    int8_t *prn_i_signed = malloc(prn_bits * DSSS_SPREADING_FACTOR);
    int8_t *prn_q_signed = malloc(prn_bits * DSSS_SPREADING_FACTOR);

    if (!prn_i_signed || !prn_q_signed) {
        free(prn_i_signed);
        free(prn_q_signed);
        free(prn_i_full);
        free(prn_q_full);
        return -1;
    }

    int8_t prn_chunk_signed[DSSS_SPREADING_FACTOR];
    for (int bit = 0; bit < prn_bits; bit++) {
        prn_generate_i(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            prn_i_signed[bit * DSSS_SPREADING_FACTOR + c] = prn_chunk_signed[c];
        }
    }

    // Reset for Q channel
    prn_init(&prn_state, 0);

    // Generate Q-channel PRN (signed -1/+1)
    for (int bit = 0; bit < prn_bits; bit++) {
        prn_generate_q(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            prn_q_signed[bit * DSSS_SPREADING_FACTOR + c] = prn_chunk_signed[c];
        }
    }

    // PHASE 1: Test 8 phase/swap combinations with default despread params
    float best_phase1_corr = 0.0f;
    int best_phase = 0;
    bool best_swap = false;

    // Default despreading parameters for Phase 1
    const int default_chip_conv = 0;  // Real>0 → 0
    const int default_prn_conv = 0;   // -1 → 1, +1 → 0
    const int default_interleave = 0; // I,Q,I,Q
    const int default_offset = -1;    // From diagnostic

    for (int p = 0; p < 4; p++) {
        for (int swap = 0; swap < 2; swap++) {
            float complex rot = cexpf(I * p * M_PI / 2.0f);

            // Demodulate to chips
            uint8_t *chips_i = malloc(preamble_chips);
            uint8_t *chips_q = malloc(preamble_chips);
            if (!chips_i || !chips_q) {
                free(chips_i); free(chips_q);
                free(prn_i_signed); free(prn_q_signed);
                free(prn_i_full); free(prn_q_full);
                return -1;
            }

            for (int i = 0; i < preamble_chips; i++) {
                float complex test_sym = symbols[i] * rot;
                if (swap) test_sym = cimagf(test_sym) + I * crealf(test_sym);

                chips_i[i] = (crealf(test_sym) >= 0) ? 0 : 1;
                chips_q[i] = (cimagf(test_sym) >= 0) ? 0 : 1;
            }

            // Convert PRN with default params
            for (int c = 0; c < prn_bits * DSSS_SPREADING_FACTOR; c++) {
                prn_i_full[c] = (prn_i_signed[c] == -1) ? 1 : 0;
                prn_q_full[c] = (prn_q_signed[c] == -1) ? 1 : 0;
            }

            // Despread with default params
            uint8_t despread_bits[DSSS_PREAMBLE_LENGTH];
            for (int bit = 0; bit < prn_bits; bit++) {
                int start_idx = bit * DSSS_SPREADING_FACTOR;
                int corr_i = 0, corr_q = 0;

                for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
                    int chip_idx = start_idx + c + default_offset;
                    if (chip_idx >= 0 && chip_idx < preamble_chips) {
                        if (chips_i[chip_idx] == prn_i_full[start_idx + c]) corr_i++;
                        if (chips_q[chip_idx] == prn_q_full[start_idx + c]) corr_q++;
                    }
                }

                uint8_t bit_i = (corr_i > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;
                uint8_t bit_q = (corr_q > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;

                despread_bits[2 * bit] = bit_i;
                despread_bits[2 * bit + 1] = bit_q;
            }

            int matches = 0;
            for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
                if (despread_bits[i] == expected_preamble[i]) matches++;
            }
            float corr = (float)matches / DSSS_PREAMBLE_LENGTH;

            if (corr > best_phase1_corr) {
                best_phase1_corr = corr;
                best_phase = p;
                best_swap = (bool)swap;
            }

            free(chips_i);
            free(chips_q);
        }
    }

    printf("[PHASE] Phase 1 complete: best=rot%d/swap%d, corr=%.1f%%\n",
           best_phase, best_swap, best_phase1_corr * 100.0f);

    // PHASE 2: Exhaustive search of despreading params on best phase
    printf("[PHASE] Phase 2: Testing 88 despreading parameter combinations...\n");

    float best_corr = best_phase1_corr;
    int best_chip_conv = default_chip_conv;
    int best_prn_conv = default_prn_conv;
    int best_interleave = default_interleave;
    int best_offset = default_offset;

    // Demodulate chips with best phase/swap
    float complex rot = cexpf(I * best_phase * M_PI / 2.0f);
    uint8_t *chips_i = malloc(preamble_chips);
    uint8_t *chips_q = malloc(preamble_chips);

    if (!chips_i || !chips_q) {
        free(chips_i); free(chips_q);
        free(prn_i_signed); free(prn_q_signed);
        free(prn_i_full); free(prn_q_full);
        return -1;
    }

    for (int i = 0; i < preamble_chips; i++) {
        float complex test_sym = symbols[i] * rot;
        if (best_swap) test_sym = cimagf(test_sym) + I * crealf(test_sym);
        chips_i[i] = (crealf(test_sym) >= 0) ? 0 : 1;
        chips_q[i] = (cimagf(test_sym) >= 0) ? 0 : 1;
    }

    // Test all despreading parameter combinations
    for (int chip_conv = 0; chip_conv < 2; chip_conv++) {
        for (int prn_conv = 0; prn_conv < 2; prn_conv++) {
            // Convert PRN based on prn_conv
            for (int c = 0; c < prn_bits * DSSS_SPREADING_FACTOR; c++) {
                prn_i_full[c] = (prn_conv == 0) ?
                    ((prn_i_signed[c] == -1) ? 1 : 0) :
                    ((prn_i_signed[c] == -1) ? 0 : 1);
                prn_q_full[c] = (prn_conv == 0) ?
                    ((prn_q_signed[c] == -1) ? 1 : 0) :
                    ((prn_q_signed[c] == -1) ? 0 : 1);
            }

            for (int interleave = 0; interleave < 2; interleave++) {
                for (int offset = -5; offset <= 5; offset++) {
                    uint8_t despread_bits[DSSS_PREAMBLE_LENGTH];

                    for (int bit = 0; bit < prn_bits; bit++) {
                        int start_idx = bit * DSSS_SPREADING_FACTOR;
                        int corr_i = 0, corr_q = 0;

                        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
                            int chip_idx = start_idx + c + offset;
                            if (chip_idx >= 0 && chip_idx < preamble_chips) {
                                uint8_t recv_chip_i = chip_conv ? (1 - chips_i[chip_idx]) : chips_i[chip_idx];
                                uint8_t recv_chip_q = chip_conv ? (1 - chips_q[chip_idx]) : chips_q[chip_idx];

                                if (recv_chip_i == prn_i_full[start_idx + c]) corr_i++;
                                if (recv_chip_q == prn_q_full[start_idx + c]) corr_q++;
                            }
                        }

                        uint8_t bit_i = (corr_i > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;
                        uint8_t bit_q = (corr_q > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;

                        if (interleave == 0) {
                            despread_bits[2 * bit] = bit_i;
                            despread_bits[2 * bit + 1] = bit_q;
                        } else {
                            despread_bits[2 * bit] = bit_q;
                            despread_bits[2 * bit + 1] = bit_i;
                        }
                    }

                    int matches = 0;
                    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
                        if (despread_bits[i] == expected_preamble[i]) matches++;
                    }
                    float corr = (float)matches / DSSS_PREAMBLE_LENGTH;

                    if (corr > best_corr) {
                        best_corr = corr;
                        best_chip_conv = chip_conv;
                        best_prn_conv = prn_conv;
                        best_interleave = interleave;
                        best_offset = offset;
                        printf("[PHASE]   NEW BEST: corr=%.1f%%, chip_conv=%d, prn_conv=%d, interleave=%d, offset=%+d\n",
                               corr * 100.0f, chip_conv, prn_conv, interleave, offset);
                    }
                }
            }
        }
    }

    free(chips_i);
    free(chips_q);
    free(prn_i_signed);
    free(prn_q_signed);
    free(prn_i_full);
    free(prn_q_full);

    printf("[PHASE] ✓ FINAL: rot=%d (%3.0f°), swap=%d, corr=%.1f%%\n",
           best_phase, best_phase * 90.0f, best_swap, best_corr * 100.0f);
    printf("[PHASE] ✓ Despread params: chip_conv=%d, prn_conv=%d, interleave=%d, offset=%+d\n",
           best_chip_conv, best_prn_conv, best_interleave, best_offset);

    if (phase_rot) *phase_rot = best_phase;
    if (iq_swap) *iq_swap = best_swap;

    // Store optimal despreading parameters in state
    if (state) {
        state->chip_convention = best_chip_conv;
        state->prn_conversion = best_prn_conv;
        state->interleaving = best_interleave;
        state->chip_offset = best_offset;
    }

    // Return success if correlation > 90% (strong match on preamble)
    if (best_corr > 0.9f) {
        return 0;
    } else if (best_corr > 0.7f) {
        fprintf(stderr, "[PHASE] Warning: Moderate correlation (%.1f%%), proceeding anyway\n",
                best_corr * 100.0f);
        return 0;
    } else {
        fprintf(stderr, "[PHASE] Error: Low correlation (%.1f%%), phase resolution failed\n",
                best_corr * 100.0f);
        return -1;
    }
}

// =============================================================================
// DSSS DESPREADING - DIAGNOSTIC TOOL
// =============================================================================

/**
 * @brief Outil de diagnostic pour identifier les paramètres optimaux de désétalement
 *
 * Teste systématiquement toutes les combinaisons de :
 * - Convention chips (Real>0 → 0 vs Real>0 → 1)
 * - Conversion PRN (-1 → 0 vs -1 → 1)
 * - Interleaving I/Q (I,Q,I,Q vs Q,I,Q,I)
 * - Offset chips (-10 à +10)
 *
 * @param chips_i Input I-channel chips
 * @param chips_q Input Q-channel chips
 * @param num_chips Total number of chips (should be 12,800 for preamble)
 * @return Best correlation found (0.0 to 1.0)
 */
static float dsss_diagnose_despreading(const uint8_t *chips_i, const uint8_t *chips_q,
                                       size_t num_chips) {
    printf("\n[DIAGNOSTIC] Testing DSSS despreading parameters...\n");
    printf("[DIAGNOSTIC] Testing preamble only (%zu chips = 50 bits)\n", num_chips);

    // Expected preamble pattern
    uint8_t expected[DSSS_PREAMBLE_LENGTH];
    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
        expected[i] = i % 2;
    }

    // Chip offsets to test
    int offsets[] = {-10, -5, -3, -2, -1, 0, 1, 2, 3, 5, 10};
    int num_offsets = sizeof(offsets) / sizeof(offsets[0]);

    float best_corr = 0.0f;
    int best_chip_conv = 0;
    int best_prn_conv = 0;
    int best_interleave = 0;
    int best_offset = 0;

    // Test all combinations
    for (int chip_conv = 0; chip_conv < 2; chip_conv++) {
        for (int prn_conv = 0; prn_conv < 2; prn_conv++) {
            for (int interleave = 0; interleave < 2; interleave++) {
                for (int o = 0; o < num_offsets; o++) {
                    int offset = offsets[o];

                    // Generate PRN for this combination
                    prn_state_t prn_state;
                    prn_init(&prn_state, 0);

                    uint8_t *prn_i_test = malloc(num_chips);
                    uint8_t *prn_q_test = malloc(num_chips);

                    if (!prn_i_test || !prn_q_test) {
                        free(prn_i_test);
                        free(prn_q_test);
                        continue;
                    }

                    // Generate I-channel PRN
                    int8_t prn_chunk[DSSS_SPREADING_FACTOR];
                    for (int bit = 0; bit < DSSS_PREAMBLE_LENGTH / 2; bit++) {
                        prn_generate_i(&prn_state, prn_chunk);
                        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
                            int idx = bit * DSSS_SPREADING_FACTOR + c;
                            // Apply PRN conversion
                            if (prn_conv == 0) {
                                prn_i_test[idx] = (prn_chunk[c] == -1) ? 1 : 0;
                            } else {
                                prn_i_test[idx] = (prn_chunk[c] == -1) ? 0 : 1;
                            }
                        }
                    }

                    // Generate Q-channel PRN
                    prn_init(&prn_state, 0);
                    for (int bit = 0; bit < DSSS_PREAMBLE_LENGTH / 2; bit++) {
                        prn_generate_q(&prn_state, prn_chunk);
                        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
                            int idx = bit * DSSS_SPREADING_FACTOR + c;
                            // Apply PRN conversion
                            if (prn_conv == 0) {
                                prn_q_test[idx] = (prn_chunk[c] == -1) ? 1 : 0;
                            } else {
                                prn_q_test[idx] = (prn_chunk[c] == -1) ? 0 : 1;
                            }
                        }
                    }

                    // Despread with current parameters
                    uint8_t despread[DSSS_PREAMBLE_LENGTH];
                    for (int bit = 0; bit < DSSS_PREAMBLE_LENGTH / 2; bit++) {
                        int corr_i = 0, corr_q = 0;

                        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
                            int chip_idx = bit * DSSS_SPREADING_FACTOR + c;
                            int test_idx = chip_idx + offset;

                            if (test_idx >= 0 && test_idx < (int)num_chips) {
                                uint8_t rx_chip_i = chips_i[test_idx];
                                uint8_t rx_chip_q = chips_q[test_idx];
                                uint8_t ref_i = prn_i_test[chip_idx];
                                uint8_t ref_q = prn_q_test[chip_idx];

                                // Apply chip convention
                                if (chip_conv == 0) {
                                    if (rx_chip_i == ref_i) corr_i++;
                                    if (rx_chip_q == ref_q) corr_q++;
                                } else {
                                    if (rx_chip_i != ref_i) corr_i++;
                                    if (rx_chip_q != ref_q) corr_q++;
                                }
                            }
                        }

                        // Majority vote
                        uint8_t bit_i = (corr_i > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;
                        uint8_t bit_q = (corr_q > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;

                        // Apply interleaving
                        if (interleave == 0) {
                            despread[2 * bit] = bit_i;
                            despread[2 * bit + 1] = bit_q;
                        } else {
                            despread[2 * bit] = bit_q;
                            despread[2 * bit + 1] = bit_i;
                        }
                    }

                    // Compare with expected preamble
                    int matches = 0;
                    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
                        if (despread[i] == expected[i]) matches++;
                    }

                    float corr = (float)matches / DSSS_PREAMBLE_LENGTH;

                    // Report good matches
                    if (corr > 0.9f) {
                        printf("[DIAGNOSTIC] ✅ EXCELLENT: %.1f%% (chip_conv=%d, prn_conv=%d, interleave=%d, offset=%+d)\n",
                               corr * 100.0f, chip_conv, prn_conv, interleave, offset);
                    } else if (corr > 0.7f) {
                        printf("[DIAGNOSTIC] ⚠️  GOOD: %.1f%% (chip_conv=%d, prn_conv=%d, interleave=%d, offset=%+d)\n",
                               corr * 100.0f, chip_conv, prn_conv, interleave, offset);
                    }

                    if (corr > best_corr) {
                        best_corr = corr;
                        best_chip_conv = chip_conv;
                        best_prn_conv = prn_conv;
                        best_interleave = interleave;
                        best_offset = offset;
                    }

                    free(prn_i_test);
                    free(prn_q_test);
                }
            }
        }
    }

    printf("\n[DIAGNOSTIC] ═══ BEST COMBINATION FOUND ═══\n");
    printf("[DIAGNOSTIC] Correlation: %.1f%%\n", best_corr * 100.0f);
    printf("[DIAGNOSTIC] Chip convention: %d (Real>0 → %d)\n", best_chip_conv, best_chip_conv == 0 ? 0 : 1);
    printf("[DIAGNOSTIC] PRN conversion: %d (-1 → %d, +1 → %d)\n",
           best_prn_conv, best_prn_conv == 0 ? 1 : 0, best_prn_conv == 0 ? 0 : 1);
    printf("[DIAGNOSTIC] Interleaving: %d (%s)\n",
           best_interleave, best_interleave == 0 ? "I,Q,I,Q" : "Q,I,Q,I");
    printf("[DIAGNOSTIC] Chip offset: %+d\n", best_offset);
    printf("[DIAGNOSTIC] ═══════════════════════════════\n\n");

    return best_corr;
}

// =============================================================================
// DSSS DESPREADING
// =============================================================================

/**
 * ⚠️  BUG #3: DESPREADING VERY LOW CORRELATION ⚠️
 *
 * SYMPTOMS:
 * - Mean correlation = 0.053 (should be >0.7)
 * - This is barely better than random (0.5)
 * - Results in ~55% bit accuracy
 *
 * SUSPECTED CAUSES:
 * 1. TIMING: PRN chips not aligned with received chips
 *    - Timing recovery bug causes wrong sampling points
 *    - Even 1-chip offset destroys correlation
 *
 * 2. CHIP CONVENTION: May still have sign/polarity issues
 *    - Current: Real>0 → chip 0, Real<0 → chip 1
 *    - PRN: -1 → chip 1, +1 → chip 0
 *    - Double-check at symbol demodulation stage
 *
 * 3. PRN SEQUENCE OFFSET: PRN may start at wrong point
 *    - Need to verify PRN init state = 0 is correct
 *    - T.018 Appendix D specifies init: I=0x000001, Q=0x000041
 *
 * 4. SPREADING FACTOR MISMATCH: Expecting 256 chips/bit
 *    - Verify generator uses exactly 256 chips
 *    - Check for off-by-one errors
 *
 * DEBUGGING APPROACH:
 * 1. Plot raw correlation vs chip offset (-256 to +256)
 * 2. Compare first 256 chips with expected PRN sequence
 * 3. Try different PRN init states
 * 4. Verify chip rate is exactly 38.4 kchips/s
 * 5. Check if timing recovery offset affects alignment
 *
 * EXPECTED BEHAVIOR:
 * - Correlation should show clear peak (>0.7) when PRN aligned
 * - Misalignment by even 1 chip should drop to ~0.5
 * - Current 0.053 suggests major synchronization failure
 */
int dsss_despread(const uint8_t *chips_i, const uint8_t *chips_q,
                  uint8_t *output_bits, float *correlation,
                  const dsss_demod_state_t *state) {
    // Generate PRN sequences for all 150 bits (I and Q channels)
    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    int8_t *prn_i_full = malloc(38400 * sizeof(int8_t));
    int8_t *prn_q_full = malloc(38400 * sizeof(int8_t));

    if (!prn_i_full || !prn_q_full) {
        free(prn_i_full);
        free(prn_q_full);
        return -1;
    }

    // Extract optimal parameters from state (found by exhaustive search)
    int chip_conv = state ? state->chip_convention : 0;
    int prn_conv = state ? state->prn_conversion : 0;
    int interleave = state ? state->interleaving : 0;
    int chip_offset = state ? state->chip_offset : -1;

    // Generate I-channel PRN (38400 chips for 150 bits)
    int8_t prn_chunk_signed[DSSS_SPREADING_FACTOR];
    for (int bit = 0; bit < 150; bit++) {
        prn_generate_i(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            // Apply PRN conversion parameter
            if (prn_conv == 0) {
                prn_i_full[bit * DSSS_SPREADING_FACTOR + c] =
                    (prn_chunk_signed[c] == -1) ? 1 : 0;
            } else {
                prn_i_full[bit * DSSS_SPREADING_FACTOR + c] =
                    (prn_chunk_signed[c] == -1) ? 0 : 1;
            }
        }
    }

    // Reset for Q channel
    prn_init(&prn_state, 0);

    // Generate Q-channel PRN
    for (int bit = 0; bit < 150; bit++) {
        prn_generate_q(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            // Apply PRN conversion parameter
            if (prn_conv == 0) {
                prn_q_full[bit * DSSS_SPREADING_FACTOR + c] =
                    (prn_chunk_signed[c] == -1) ? 1 : 0;
            } else {
                prn_q_full[bit * DSSS_SPREADING_FACTOR + c] =
                    (prn_chunk_signed[c] == -1) ? 0 : 1;
            }
        }
    }

    // Despread using optimal parameters from exhaustive search
    float total_corr = 0.0f;

    for (int bit = 0; bit < 150; bit++) {
        int start_idx = bit * DSSS_SPREADING_FACTOR;

        // Despread I-channel with optimal parameters
        int corr_i = 0;
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            int chip_idx = start_idx + c + chip_offset;
            if (chip_idx >= 0 && chip_idx < 38400) {
                // Apply chip convention
                uint8_t recv_chip = chip_conv ? (1 - chips_i[chip_idx]) : chips_i[chip_idx];
                if (recv_chip == prn_i_full[start_idx + c]) {
                    corr_i++;
                }
            }
        }

        // Despread Q-channel with optimal parameters
        int corr_q = 0;
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            int chip_idx = start_idx + c + chip_offset;
            if (chip_idx >= 0 && chip_idx < 38400) {
                // Apply chip convention
                uint8_t recv_chip = chip_conv ? (1 - chips_q[chip_idx]) : chips_q[chip_idx];
                if (recv_chip == prn_q_full[start_idx + c]) {
                    corr_q++;
                }
            }
        }

        // ML decision (majority vote)
        uint8_t bit_i = (corr_i > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;
        uint8_t bit_q = (corr_q > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;

        // Apply interleaving parameter
        if (interleave == 0) {
            // Standard: odd bits from I, even bits from Q
            output_bits[2 * bit] = bit_i;
            output_bits[2 * bit + 1] = bit_q;
        } else {
            // Swapped: odd bits from Q, even bits from I
            output_bits[2 * bit] = bit_q;
            output_bits[2 * bit + 1] = bit_i;
        }

        // Track correlation
        float norm_corr_i = fabsf((float)corr_i - DSSS_SPREADING_FACTOR / 2) /
                           (DSSS_SPREADING_FACTOR / 2);
        float norm_corr_q = fabsf((float)corr_q - DSSS_SPREADING_FACTOR / 2) /
                           (DSSS_SPREADING_FACTOR / 2);
        total_corr += (norm_corr_i + norm_corr_q) / 2.0f;
    }

    free(prn_i_full);
    free(prn_q_full);

    if (correlation) *correlation = total_corr / 150.0f;

    return 0;
}

// =============================================================================
// MAIN DEMODULATOR
// =============================================================================

int dsss_demodulate(const float complex *iq_samples, size_t num_samples,
                    uint8_t *output_bits, float samp_rate,
                    dsss_demod_state_t *state) {

    if (!iq_samples || !output_bits) {
        fprintf(stderr, "Error: NULL input\n");
        return -1;
    }

    // Initialize state
    dsss_demod_state_t local_state = {0};

    // Step 1: AGC
    printf("Step 1: AGC...\n");
    float complex *agc_out = malloc(num_samples * sizeof(float complex));
    if (!agc_out) return -1;

    dsss_agc(iq_samples, agc_out, num_samples, &local_state.agc_gain);
    printf("  AGC gain: %.2f\n", local_state.agc_gain);

    // Step 2: Preamble detection
    printf("Step 2: Preamble detection...\n");
    int preamble_idx;
    float freq_offset_coarse;

    if (dsss_detect_preamble(agc_out, num_samples, samp_rate,
                            &preamble_idx, &freq_offset_coarse,
                            &local_state.correlation_peak) < 0) {
        free(agc_out);
        if (state) *state = local_state;
        return -2;
    }

    local_state.preamble_found = true;
    local_state.preamble_index = preamble_idx;
    local_state.coarse_freq_offset = freq_offset_coarse;

    // Extract burst from detected position
    // We need samples for the entire frame duration
    // 150 bits/channel × 256 chips/bit = 38,400 chips at 38.4 kchips/s = 1.0 sec
    // But after preamble detection, we can use remaining samples
    size_t ideal_burst_length = (DSSS_TOTAL_BITS / 2) * DSSS_SPREADING_FACTOR *
                                (size_t)(samp_rate / DSSS_CHIP_RATE + 0.5f);

    size_t available_samples = num_samples - preamble_idx;
    size_t burst_length = (available_samples < ideal_burst_length) ?
                          available_samples : ideal_burst_length;

    // Require at least 80% of ideal length
    size_t min_burst_length = (ideal_burst_length * 4) / 5;
    if (burst_length < min_burst_length) {
        fprintf(stderr, "Error: Incomplete burst (have %zu, need %zu, ideal %zu)\n",
                burst_length, min_burst_length, ideal_burst_length);
        free(agc_out);
        return -1;
    }

    printf("  Burst length: %zu samples (%.2f sec)\n",
           burst_length, (float)burst_length / samp_rate);

    float complex *burst = &agc_out[preamble_idx];

    // Step 3: Coarse frequency correction
    printf("Step 3: Frequency correction (coarse: %.1f Hz)...\n", freq_offset_coarse);
    float complex *freq_corr_out = malloc(burst_length * sizeof(float complex));
    if (!freq_corr_out) {
        free(agc_out);
        return -1;
    }

    dsss_frequency_correction(burst, freq_corr_out, burst_length,
                             freq_offset_coarse, samp_rate);

    // =========================================================================
    // Step 4: Fine frequency correction (DISABLED - Costas loop diverges)
    // =========================================================================
    //
    // ⚠️  BUG #4: COSTAS LOOP DIVERGENCE ⚠️
    //
    // PROBLEM: Costas loop estimates +10.6 kHz offset on signal centered at -0.164 kHz
    // (verified by user on GQRX spectrum analyzer)
    //
    // SYMPTOMS:
    // - Loop diverges instead of tracking residual frequency error
    // - Creates large artificial frequency offset
    // - SNR drops from ~3 dB to 0.3 dB when enabled
    //
    // SUSPECTED CAUSES:
    // 1. Loop bandwidth (0.01) may be too large for DSSS signal
    // 2. Phase error detector may be incorrect for QPSK
    // 3. Costas loop unstable on spread-spectrum signal
    // 4. May need to operate on despread signal instead
    //
    // CURRENT WORKAROUND: Disabled, using coarse correction only
    // - Coarse correction handles frequency offset adequately
    // - Residual offset appears small (<1 kHz) based on results
    //
    // FUTURE FIX OPTIONS:
    // 1. Reduce loop bandwidth significantly (try 0.001 or 0.0001)
    // 2. Use different phase detector (e.g., decision-directed)
    // 3. Apply fine tracking AFTER despreading
    // 4. Use FLL (Frequency Lock Loop) instead of PLL
    //
    printf("Step 4: Fine frequency sync (DISABLED - using coarse only)...\n");
    float complex *fine_sync_out = malloc(burst_length * sizeof(float complex));
    if (!fine_sync_out) {
        free(agc_out);
        free(freq_corr_out);
        return -1;
    }

    // TEMPORARY: Skip Costas loop, use coarse correction only
    memcpy(fine_sync_out, freq_corr_out, burst_length * sizeof(float complex));
    float freq_offset_fine = 0.0f;

    // dsss_fine_frequency_sync(freq_corr_out, fine_sync_out, burst_length,
    //                         samp_rate, &freq_offset_fine);
    local_state.fine_freq_offset = freq_offset_fine;
    local_state.total_freq_offset = freq_offset_coarse + freq_offset_fine;
    printf("  Fine freq offset: %.1f Hz (total: %.1f Hz)\n",
           freq_offset_fine, local_state.total_freq_offset);

    // Step 4.5: OQPSK to QPSK conversion (apply Tc/2 delay)
    printf("Step 4.5: OQPSK→QPSK conversion (Tc/2 delay)...\n");
    float complex *qpsk_out = malloc(burst_length * sizeof(float complex));
    if (!qpsk_out) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
        return -1;
    }

    int sps_main = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);
    oqpsk_to_qpsk(fine_sync_out, qpsk_out, burst_length, sps_main);
    printf("  Applied Tc/2 delay: %d samples\n", sps_main / 2);

    // Update burst_length to account for delay
    size_t qpsk_length = burst_length - sps_main / 2;

    // Step 5: Timing recovery
    printf("Step 5: Timing recovery...\n");
    float complex *symbols = malloc(40000 * sizeof(float complex));
    if (!symbols) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
        free(qpsk_out);
        return -1;
    }

    size_t num_symbols;
    dsss_timing_recovery(qpsk_out, symbols, qpsk_length,
                        &num_symbols, samp_rate);
    printf("  Recovered %zu symbols\n", num_symbols);

    // Step 6: Phase ambiguity resolution
    printf("Step 6: Phase ambiguity resolution...\n");
    int phase_rot;
    bool iq_swap;

    if (dsss_resolve_phase_ambiguity(symbols, num_symbols,
                                    &phase_rot, &iq_swap, &local_state) < 0) {
        fprintf(stderr, "Warning: Phase ambiguity resolution uncertain\n");
    }

    local_state.phase_rotation = phase_rot;
    local_state.iq_swapped = iq_swap;

    // Apply phase rotation and I/Q swap
    float complex rot = cexpf(I * phase_rot * M_PI / 2.0f);
    for (size_t i = 0; i < num_symbols; i++) {
        symbols[i] *= rot;
        if (iq_swap) {
            symbols[i] = cimagf(symbols[i]) + I * crealf(symbols[i]);
        }
    }

    // Step 7: QPSK demodulation to chips
    printf("Step 7: QPSK demodulation...\n");
    uint8_t *chips_i = malloc(num_symbols);
    uint8_t *chips_q = malloc(num_symbols);

    if (!chips_i || !chips_q) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
        free(qpsk_out);
        free(symbols);
        free(chips_i);
        free(chips_q);
        return -1;
    }

    // Convert QPSK symbols to chips (0/1)
    // Convention: positive real → 0, negative real → 1 (inverted phase)
    for (size_t i = 0; i < num_symbols; i++) {
        chips_i[i] = (crealf(symbols[i]) >= 0) ? 0 : 1;
        chips_q[i] = (cimagf(symbols[i]) >= 0) ? 0 : 1;
    }

    // =========================================================================
    // DIAGNOSTIC TOOL: Test all despreading parameter combinations
    // =========================================================================
    // Run diagnostic on preamble (50 bits = 12,800 chips) to find optimal params
    const size_t preamble_chips = DSSS_PREAMBLE_LENGTH * DSSS_SPREADING_FACTOR;
    if (num_symbols >= preamble_chips) {
        float diag_corr = dsss_diagnose_despreading(chips_i, chips_q, preamble_chips);
        if (diag_corr < 0.7f) {
            fprintf(stderr, "Warning: Diagnostic found no good parameters (best=%.1f%%)\n",
                    diag_corr * 100.0f);
        }
    } else {
        fprintf(stderr, "Warning: Not enough chips for diagnostic (%zu < %zu)\n",
                num_symbols, preamble_chips);
    }
    // =========================================================================

    // Step 8: DSSS despreading
    printf("Step 8: DSSS despreading...\n");
    float mean_corr;

    if (dsss_despread(chips_i, chips_q, output_bits, &mean_corr, &local_state) < 0) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
        free(qpsk_out);
        free(symbols);
        free(chips_i);
        free(chips_q);
        return -1;
    }

    local_state.mean_correlation = mean_corr;
    local_state.bits_decoded = DSSS_TOTAL_BITS;

    printf("  Mean correlation: %.3f\n", mean_corr);

    // Estimate SNR
    local_state.snr_estimate = estimate_snr(symbols, num_symbols);
    printf("  Estimated SNR: %.1f dB\n", local_state.snr_estimate);

    // Cleanup
    free(agc_out);
    free(freq_corr_out);
    free(fine_sync_out);
    free(qpsk_out);
    free(symbols);
    free(chips_i);
    free(chips_q);

    if (state) *state = local_state;

    printf("✓ Demodulation complete: %d bits recovered\n", DSSS_TOTAL_BITS);
    return 0;
}

// =============================================================================
// FILE LOADING
// =============================================================================

int dsss_demodulate_file(const char *filename, uint8_t *output_bits,
                         dsss_demod_state_t *state) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    size_t num_samples = file_size / (2 * sizeof(float));  // Complex float

    printf("Loading IQ file: %s\n", filename);
    printf("  File size: %ld bytes\n", file_size);
    printf("  Samples: %zu (%.3f seconds @ 2.5 MHz)\n",
           num_samples, num_samples / 2.5e6);

    // Allocate and load
    float complex *iq_samples = malloc(num_samples * sizeof(float complex));
    if (!iq_samples) {
        fclose(fp);
        return -1;
    }

    size_t read = fread(iq_samples, sizeof(float complex), num_samples, fp);
    fclose(fp);

    if (read != num_samples) {
        fprintf(stderr, "Warning: Read %zu samples, expected %zu\n", read, num_samples);
        num_samples = read;
    }

    // Demodulate
    int result = dsss_demodulate(iq_samples, num_samples, output_bits,
                                 DSSS_SAMPLE_RATE, state);

    free(iq_samples);
    return result;
}

// =============================================================================
// UTILITY
// =============================================================================

void dsss_print_state(const dsss_demod_state_t *state) {
    if (!state) return;

    printf("\n=== DSSS Demodulator State ===\n");
    printf("Preamble: %s at index %d (corr: %.3f)\n",
           state->preamble_found ? "FOUND" : "NOT FOUND",
           state->preamble_index, state->correlation_peak);
    printf("Frequency: coarse=%.1f Hz, fine=%.1f Hz, total=%.1f Hz\n",
           state->coarse_freq_offset, state->fine_freq_offset,
           state->total_freq_offset);
    printf("Phase: rotation=%d (%.0f°), I/Q swapped=%s\n",
           state->phase_rotation, state->phase_rotation * 90.0f,
           state->iq_swapped ? "YES" : "NO");
    printf("Quality: SNR=%.1f dB, AGC gain=%.2f\n",
           state->snr_estimate, state->agc_gain);
    printf("DSSS: mean_corr=%.3f, bits_decoded=%d\n",
           state->mean_correlation, state->bits_decoded);
    printf("==============================\n\n");
}
