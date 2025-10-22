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
// TIMING RECOVERY
// =============================================================================

/**
 * ⚠️  BUG #1: TIMING RECOVERY INCORRECT ⚠️
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
    const float detector_gain = 4.0f;
    const int sps = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);

    // Loop filter gains
    float k1 = 4.0f * damping * loop_bw;
    float k2 = 4.0f * loop_bw * loop_bw;

    size_t sample_idx = 0;
    size_t symbol_count = 0;
    float timing_phase = 0.0f;

    float complex prev_prev_sym = 0.0f;
    float complex prev_sym = 0.0f;

    while (sample_idx + sps < num_samples && symbol_count < 40000) {
        // Extract symbol sample
        int start_idx = (int)(sample_idx + 0.5f);
        if (start_idx + sps >= num_samples) break;

        // Simple averaging over samples per symbol
        float complex symbol = 0.0f;
        for (int i = 0; i < sps; i++) {
            symbol += input[start_idx + i];
        }
        symbol /= sps;

        output[symbol_count] = symbol;
        symbol_count++;

        // Gardner timing error detector
        float timing_error = 0.0f;
        if (symbol_count >= 3) {
            float complex early = prev_prev_sym;
            float complex prompt = prev_sym;
            float complex late = symbol;

            // Gardner TED
            timing_error = crealf((late - early) * conjf(prompt));
        }

        // Update timing with loop filter
        timing_phase += k1 * timing_error;
        sample_idx += sps + k2 * timing_error + timing_phase;

        prev_prev_sym = prev_sym;
        prev_sym = symbol;
    }

    if (num_symbols) *num_symbols = symbol_count;
    return 0;
}

// =============================================================================
// PHASE AMBIGUITY RESOLUTION
// =============================================================================

/**
 * ⚠️  BUG #2: PHASE AMBIGUITY TESTED ON WRONG DATA ⚠️
 *
 * CRITICAL ERROR: This function tests phase rotation on SPREAD CHIPS!
 * It should test AFTER despreading to get clean preamble bits.
 *
 * CURRENT (WRONG):
 *   symbols → test 8 phase combinations → select best
 *            ↑ These are DSSS-spread QPSK symbols!
 *
 * CORRECT APPROACH:
 *   symbols → despread → extract 50-bit preamble → test phase → apply
 *
 * SYMPTOMS:
 * - Selects rotation=1 (90°) with only 48.6% correlation (random!)
 * - Cannot distinguish correct phase because spread signal looks like noise
 *
 * WHY THIS IS WRONG:
 * - DSSS spreading makes symbols appear random (that's the point!)
 * - Testing phase on spread symbols compares noise to expected pattern
 * - Only AFTER despreading do we get clean bits for phase testing
 *
 * CORRECT IMPLEMENTATION REQUIRES:
 * 1. Move this function AFTER despreading (reorder pipeline)
 * 2. Test on 50-bit preamble: expected = [0,1,0,1,0,1,...]
 * 3. Should get >90% correlation when phase is correct
 * 4. Alternative: Use preamble BEFORE spreading for detection (needs DSSS matched filter)
 *
 * See T.018 Section 2.2.3.b for OQPSK structure.
 */
int dsss_resolve_phase_ambiguity(const float complex *symbols, size_t num_symbols,
                                 int *phase_rot, bool *iq_swap) {
    // Generate expected preamble pattern
    const int test_length = 3200;  // Test first 3200 chips
    uint8_t expected_chips[test_length];

    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    int8_t prn_i[DSSS_SPREADING_FACTOR];
    int8_t prn_q[DSSS_SPREADING_FACTOR];

    int chip_idx = 0;
    for (int bit = 0; bit < test_length / DSSS_SPREADING_FACTOR && chip_idx < test_length; bit++) {
        prn_generate_i(&prn_state, prn_i);
        prn_generate_q(&prn_state, prn_q);

        for (int c = 0; c < DSSS_SPREADING_FACTOR && chip_idx < test_length; c++) {
            expected_chips[chip_idx++] = (bit % 2 == 0) ? 0 : 1;  // Alternating pattern
        }
    }

    // Test all phase rotations and I/Q swaps
    float best_corr = 0.0f;
    int best_phase = 0;
    bool best_swap = false;

    for (int p = 0; p < 4; p++) {  // 4 phase rotations
        for (int swap = 0; swap < 2; swap++) {  // I/Q swap
            // Apply phase rotation
            float complex rot = cexpf(I * p * M_PI / 2.0f);

            int matches = 0;
            int total = fmin(test_length, num_symbols);

            for (int i = 0; i < total; i++) {
                float complex test_sym = symbols[i] * rot;

                // I/Q swap if needed
                if (swap) {
                    test_sym = cimagf(test_sym) + I * crealf(test_sym);
                }

                // Demodulate QPSK
                uint8_t demod_bit = (crealf(test_sym) >= 0 && cimagf(test_sym) >= 0) ? 0 : 1;

                if (demod_bit == expected_chips[i]) {
                    matches++;
                }
            }

            float corr = (float)matches / total;
            if (corr > best_corr) {
                best_corr = corr;
                best_phase = p;
                best_swap = (bool)swap;
            }
        }
    }

    printf("Phase ambiguity resolved: rotation=%d, swap=%d, corr=%.1f%%\n",
           best_phase, best_swap, best_corr * 100.0f);

    if (phase_rot) *phase_rot = best_phase;
    if (iq_swap) *iq_swap = best_swap;

    return (best_corr > 0.5f) ? 0 : -1;
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
                  uint8_t *output_bits, float *correlation) {
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

    // Generate I-channel PRN (38400 chips for 150 bits)
    // Convert from (-1, +1) to (1, 0) for XOR comparison
    int8_t prn_chunk_signed[DSSS_SPREADING_FACTOR];
    for (int bit = 0; bit < 150; bit++) {
        prn_generate_i(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            // PRN: -1 → chip 1, +1 → chip 0
            prn_i_full[bit * DSSS_SPREADING_FACTOR + c] =
                (prn_chunk_signed[c] == -1) ? 1 : 0;
        }
    }

    // Reset for Q channel
    prn_init(&prn_state, 0);

    // Generate Q-channel PRN
    for (int bit = 0; bit < 150; bit++) {
        prn_generate_q(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            // PRN: -1 → chip 1, +1 → chip 0
            prn_q_full[bit * DSSS_SPREADING_FACTOR + c] =
                (prn_chunk_signed[c] == -1) ? 1 : 0;
        }
    }

    // Despread using XOR correlation
    float total_corr = 0.0f;

    for (int bit = 0; bit < 150; bit++) {
        int start_idx = bit * DSSS_SPREADING_FACTOR;

        // Despread I-channel
        int corr_i = 0;
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            if (chips_i[start_idx + c] == prn_i_full[start_idx + c]) {
                corr_i++;
            }
        }

        // Despread Q-channel
        int corr_q = 0;
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            if (chips_q[start_idx + c] == prn_q_full[start_idx + c]) {
                corr_q++;
            }
        }

        // ML decision (majority vote)
        uint8_t bit_i = (corr_i > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;
        uint8_t bit_q = (corr_q > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;

        // Interleave I/Q → output (odd bits from I, even bits from Q)
        output_bits[2 * bit] = bit_i;
        output_bits[2 * bit + 1] = bit_q;

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

    // Step 5: Timing recovery
    printf("Step 5: Timing recovery...\n");
    float complex *symbols = malloc(40000 * sizeof(float complex));
    if (!symbols) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
        return -1;
    }

    size_t num_symbols;
    dsss_timing_recovery(fine_sync_out, symbols, burst_length,
                        &num_symbols, samp_rate);
    printf("  Recovered %zu symbols\n", num_symbols);

    // Step 6: Phase ambiguity resolution
    printf("Step 6: Phase ambiguity resolution...\n");
    int phase_rot;
    bool iq_swap;

    if (dsss_resolve_phase_ambiguity(symbols, num_symbols,
                                    &phase_rot, &iq_swap) < 0) {
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

    // Step 8: DSSS despreading
    printf("Step 8: DSSS despreading...\n");
    float mean_corr;

    if (dsss_despread(chips_i, chips_q, output_bits, &mean_corr) < 0) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
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
