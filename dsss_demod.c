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
#include <omp.h>

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
/*static void oqpsk_to_qpsk(const float complex *oqpsk, float complex *qpsk,
                          size_t num_samples, int sps) {
    int delay = sps / 2;
    
    // Remplir la partie principale
    for (size_t i = 0; i < num_samples - delay; i++) {
        qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[i + delay]);
    }
    
    // REMPLIR AUSSI LA FIN DU BUFFER - CORRECTION CRITIQUE
    for (size_t i = num_samples - delay; i < num_samples; i++) {
        if (i < num_samples - delay) {
            qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[i + delay]);
        } else {
            // Pour les derniers échantillons, utiliser la dernière valeur disponible
            qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[num_samples - 1]);
        }
    }
    
    printf("[DEBUG] OQPSK→QPSK: qpsk[0]=%.3f+j%.3f, qpsk[1]=%.3f+j%.3f\n", 
           crealf(qpsk[0]), cimagf(qpsk[0]),
           crealf(qpsk[1]), cimagf(qpsk[1]));
}*/

/*static void oqpsk_to_qpsk(const float complex *oqpsk, float complex *qpsk,
                          size_t num_samples, int sps) {
    int delay = sps / 2;
    
    // CORRECTION: Commencer à l'index 'delay' pour éviter les zéros
    for (size_t i = 0; i < num_samples - delay; i++) {
        qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[i + delay]);
    }
    
    // Remplir la fin avec les dernières valeurs valides
    for (size_t i = num_samples - delay; i < num_samples; i++) {
        qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[num_samples - 1]);
    }
    
    printf("[DEBUG OQPSK] First 3 samples: [0]=%.3f+j%.3f, [1]=%.3f+j%.3f, [2]=%.3f+j%.3f\n",
           crealf(qpsk[0]), cimagf(qpsk[0]),
           crealf(qpsk[1]), cimagf(qpsk[1]),
           crealf(qpsk[2]), cimagf(qpsk[2]));
}*/

static void oqpsk_to_qpsk(const float complex *oqpsk, float complex *qpsk,
                          size_t num_samples, int sps) {
    int delay = sps / 2;
    
    for (size_t i = 0; i < num_samples; i++) {
        float i_val = crealf(oqpsk[i]);
        float q_val = (i >= delay) ? cimagf(oqpsk[i - delay]) : 0.0f;
        qpsk[i] = i_val + I * q_val;
    }
    
    // Debug minimal
    printf("[FIX] qpsk[0]=%.3f+j%.3f, qpsk[32]=%.3f+j%.3f\n", 
           crealf(qpsk[0]), cimagf(qpsk[0]),
           crealf(qpsk[32]), cimagf(qpsk[32]));
}

/**
 * @brief Apply lowpass filter to improve SNR
 * IIR Butterworth 1st order, cutoff frequency 3 kHz
 *
 * NOTE: Paradoxically, 3 kHz cutoff gives better phase detection (85%) than no filter (78%)
 * even though it degrades SNR. This may be due to phase equalization effect.
 */
/*static void apply_lowpass_filter(float complex *signal, size_t num_samples, float samp_rate) {
    const float cutoff_freq = 3000.0f;  // 3 kHz cutoff (empirically optimal for phase detection)
    float rc = 1.0f / (2.0f * M_PI * cutoff_freq);
    float dt = 1.0f / samp_rate;
    float alpha = dt / (rc + dt);

    float complex prev = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float complex current = signal[i];
        signal[i] = prev + alpha * (current - prev);
        prev = signal[i];
    }
}*/

/**
 * @brief Estimate SNR from signal
 */
static float estimate_snr(const float complex *signal, size_t len) {
    if (len == 0) return 0.0f;
    
    // Calcul de la puissance du signal
    float sig_power = 0.0f;
    for (size_t i = 0; i < len; i++) {
        sig_power += cabsf(signal[i]) * cabsf(signal[i]);
    }
    sig_power /= len;

    // Estimation du bruit par variance des points par rapport aux décisions idéales
    float noise_power = 0.0f;
    int count = 0;
    
    for (size_t i = 0; i < len; i++) {
        // Points QPSK idéaux : (±1 ± j*1)/√2
        float complex ideal;
        if (crealf(signal[i]) >= 0 && cimagf(signal[i]) >= 0) {
            ideal = (1.0f + I * 1.0f) / sqrtf(2.0f);
        } else if (crealf(signal[i]) >= 0 && cimagf(signal[i]) < 0) {
            ideal = (1.0f - I * 1.0f) / sqrtf(2.0f);
        } else if (crealf(signal[i]) < 0 && cimagf(signal[i]) >= 0) {
            ideal = (-1.0f + I * 1.0f) / sqrtf(2.0f);
        } else {
            ideal = (-1.0f - I * 1.0f) / sqrtf(2.0f);
        }
        
        float error = cabsf(signal[i] - ideal);
        noise_power += error * error;
        count++;
    }
    
    if (count > 0 && noise_power > 0) {
        noise_power /= count;
        return 10.0f * log10f(sig_power / noise_power);
    }
    
    return 30.0f; // SNR élevé par défaut
}


/**
 * @brief Teste une convention de conversion symbole→chip
 * @param symbols Symboles QPSK en entrée
 * @param num_symbols Nombre de symboles
 * @param conv_i Convention pour I: 0=real>0→0, 1=real>0→1
 * @param conv_q Convention pour Q: 0=imag>0→0, 1=imag>0→1  
 * @param invert Inversion des bits
 * @return Corrélation avec le PRN attendu [0.0 - 1.0]
 */
static float test_chip_convention(const float complex *symbols, size_t num_symbols,
                                 int conv_i, int conv_q, bool invert) {
    if (num_symbols < 1000) return 0.0f;
    
    uint8_t chips_i[1000], chips_q[1000];
    
    // Appliquer la convention de conversion testée
    for (int i = 0; i < 1000; i++) {
        chips_i[i] = (crealf(symbols[i]) >= 0) ? conv_i : (1 - conv_i);
        chips_q[i] = (cimagf(symbols[i]) >= 0) ? conv_q : (1 - conv_q);
        
        if (invert) {
            chips_i[i] = 1 - chips_i[i];
            chips_q[i] = 1 - chips_q[i];
        }
    }
    
    // Générer le PRN de référence
    prn_state_t prn_ref;
    prn_init(&prn_ref, 0);
    int8_t prn_i[256], prn_q[256];
    prn_generate_i(&prn_ref, prn_i);
    prn_generate_q(&prn_ref, prn_q);
    
    // Comparer avec les premiers chips PRN
    int matches_i = 0, matches_q = 0;
    for (int i = 0; i < 10; i++) {
        uint8_t expected_i = (prn_i[i] == -1) ? 1 : 0;
        uint8_t expected_q = (prn_q[i] == -1) ? 1 : 0;
        
        if (chips_i[i] == expected_i) matches_i++;
        if (chips_q[i] == expected_q) matches_q++;
    }
    
    return (float)(matches_i + matches_q) / 20.0f;  // Normalisé [0.0-1.0]
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
// SAMPLE RATE ESTIMATION
// =============================================================================

/**
 * @brief Generate DSSS preamble reference at chip rate
 *
 * Preamble pattern: 50 bits alternating 0,1,0,1,...
 * After DSSS spreading: 25×256 = 6,400 chips (I and Q channels)
 */
static int generate_preamble_chips_for_corr(float complex *preamble_chips) {
    prn_state_t prn_state_i, prn_state_q;
    prn_init(&prn_state_i, 0);  // Normal mode I-channel
    prn_init(&prn_state_q, 0);  // Normal mode Q-channel

    int8_t prn_i_buf[DSSS_SPREADING_FACTOR];
    int8_t prn_q_buf[DSSS_SPREADING_FACTOR];

    int chip_idx = 0;

    // Generate 25 bits per channel (I and Q interleaved = 50 bits total)
    for (int bit = 0; bit < 25; bit++) {
        // Generate PRN sequences for this bit
        prn_generate_i(&prn_state_i, prn_i_buf);
        prn_generate_q(&prn_state_q, prn_q_buf);

        // Preamble pattern: alternating 0,1,0,1...
        int bit_value_i = (bit * 2) % 2;        // Even positions: 0,1,0,1...
        int bit_value_q = (bit * 2 + 1) % 2;    // Odd positions: 1,0,1,0...

        // Spread each bit across 256 chips
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            // Convert PRN from {-1,+1} to chips considering bit value
            float chip_i = (bit_value_i == 0) ? (float)prn_i_buf[c] : -(float)prn_i_buf[c];
            float chip_q = (bit_value_q == 0) ? (float)prn_q_buf[c] : -(float)prn_q_buf[c];

            preamble_chips[chip_idx++] = chip_i + I * chip_q;
        }
    }

    return chip_idx;  // Should be 6400
}

/**
 * @brief Upsample preamble chips to match signal sample rate
 */
static int upsample_preamble_for_corr(const float complex *preamble_chips, int num_chips,
                                      float complex *preamble_samples, float sps) {
    int num_samples = (int)(num_chips * sps);

    for (int i = 0; i < num_samples; i++) {
        int chip_idx = (int)(i / sps);
        if (chip_idx >= num_chips) chip_idx = num_chips - 1;
        preamble_samples[i] = preamble_chips[chip_idx];
    }

    return num_samples;
}

/**
 * @brief Apply OQPSK Tc/2 compensation to preamble (QPSK conversion)
 *
 * CORRECTED: Advances Q by Tc/2 to compensate TX delay (consistent with oqpsk_to_qpsk)
 * TX sends: I(t) + j·Q(t - Tc/2)
 * RX creates: I(t) + j·Q(t) for correlation
 */
static void apply_oqpsk_delay_for_corr(const float complex *preamble, float complex *output,
                                       int num_samples, float sps) {
    int delay = (int)(sps / 2.0f);

    for (int i = 0; i < num_samples - delay; i++) {
        float i_val = crealf(preamble[i]);
        // FIXED: Advance Q instead of delaying (consistent with oqpsk_to_qpsk)
        float q_val = cimagf(preamble[i + delay]);  // Q(t + Tc/2) to compensate TX Q(t - Tc/2)
        output[i] = i_val + I * q_val;
    }

    // Fill remaining samples with zeros
    for (int i = num_samples - delay; i < num_samples; i++) {
        output[i] = 0.0f;
    }
}

/**
 * @brief Normalized cross-correlation between two complex signals
 */
static float correlate_complex_normalized(const float complex *sig1, const float complex *sig2, int len) {
    float complex corr_sum = 0.0f;
    float power1 = 0.0f, power2 = 0.0f;

    for (int i = 0; i < len; i++) {
        corr_sum += sig1[i] * conjf(sig2[i]);
        power1 += cabsf(sig1[i]) * cabsf(sig1[i]);
        power2 += cabsf(sig2[i]) * cabsf(sig2[i]);
    }

    if (power1 > 0 && power2 > 0) {
        return cabsf(corr_sum) / sqrtf(power1 * power2);
    }
    return 0.0f;
}

/**
 * @brief Real PRN-based preamble correlation for sample rate estimation
 *
 * Generates actual DSSS preamble with PRN sequences and OQPSK modulation,
 * then correlates with received signal using sliding window.
 *
 * @param signal Input signal
 * @param signal_len Signal length
 * @param samp_rate Sample rate candidate
 * @return Maximum normalized correlation found [0.0, 1.0]
 */
static float fast_preamble_correlation(const float complex *signal, size_t signal_len,
                                       float samp_rate) {
    // Calculate samples per chip
    float sps = samp_rate / DSSS_CHIP_RATE;

    // Generate preamble at chip rate
    const int num_preamble_chips = 25 * DSSS_SPREADING_FACTOR;  // 6400 chips
    float complex *preamble_chips = malloc(num_preamble_chips * sizeof(float complex));
    if (!preamble_chips) return 0.0f;

    generate_preamble_chips_for_corr(preamble_chips);

    // Upsample to match signal sample rate
    int preamble_samples_len = (int)(num_preamble_chips * sps);
    if (preamble_samples_len <= 0 || preamble_samples_len > 1000000) {
        free(preamble_chips);
        return 0.0f;
    }

    float complex *preamble_qpsk = malloc(preamble_samples_len * sizeof(float complex));
    if (!preamble_qpsk) {
        free(preamble_chips);
        return 0.0f;
    }

    upsample_preamble_for_corr(preamble_chips, num_preamble_chips, preamble_qpsk, sps);

    // Apply OQPSK Tc/2 delay
    float complex *preamble_oqpsk = malloc(preamble_samples_len * sizeof(float complex));
    if (!preamble_oqpsk) {
        free(preamble_chips);
        free(preamble_qpsk);
        return 0.0f;
    }

    apply_oqpsk_delay_for_corr(preamble_qpsk, preamble_oqpsk, preamble_samples_len, sps);

    // Sliding correlation (search first 50% of signal)
    float max_corr = 0.0f;
    int search_len = signal_len / 2;
    if (search_len < preamble_samples_len) {
        free(preamble_chips);
        free(preamble_qpsk);
        free(preamble_oqpsk);
        return 0.0f;
    }

    int step = (int)sps;  // Step by 1 chip
    if (step < 1) step = 1;

    for (int offset = 0; offset <= search_len - preamble_samples_len; offset += step) {
        float corr = correlate_complex_normalized(&signal[offset], preamble_oqpsk, preamble_samples_len);
        if (corr > max_corr) max_corr = corr;
    }

    free(preamble_chips);
    free(preamble_qpsk);
    free(preamble_oqpsk);

    return max_corr;
}

/**
 * @brief Estimate sample rate by testing multiple candidates
 * @param samples Input signal
 * @param num_samples Signal length
 * @return Estimated sample rate (Hz)
 */
float dsss_estimate_sample_rate(const float complex *samples, size_t num_samples) {
    // Extended candidate list with intermediate values
    float candidates[] = {
        300000.0f,    // 7.8 sps/chip
        384000.0f,    // 10 sps/chip
        400000.0f,    // 10.4 sps/chip
        768000.0f,    // 20 sps/chip
        1536000.0f,   // 40 sps/chip
        2000000.0f,   // 52.1 sps/chip
        2500000.0f,   // 65.1 sps/chip (PlutoSDR default)
        3072000.0f,   // 80 sps/chip
        6144000.0f    // 160 sps/chip
    };
    int num_candidates = sizeof(candidates) / sizeof(candidates[0]);

    printf("=== SAMPLE RATE ESTIMATION ===\n");
    printf("Testing %d candidate sample rates...\n", num_candidates);

    float best_corr = 0.0f;
    float best_sr = candidates[0];
    int valid_candidates = 0;

    for (int i = 0; i < num_candidates; i++) {
        float sr_candidate = candidates[i];

        // Check if file has enough samples for this sample rate
        // Need: 300 bits × 256 chips/bit × sps = 76800 × (sr / 38400)
        // Require at least 100% of full frame for reliable demodulation
        size_t min_samples = (size_t)(76800.0f * (sr_candidate / DSSS_CHIP_RATE) * 1.0f);

        if (num_samples < min_samples) {
            printf("  [%d] SR=%.0f Hz (%.1f sps/chip): SKIPPED (need %zu samples, have %zu)\n",
                   i+1, sr_candidate, sr_candidate / DSSS_CHIP_RATE, min_samples, num_samples);
            continue;
        }

        // Test with ±5% tolerance
        for (float delta = -0.05f; delta <= 0.05f; delta += 0.025f) {
            float current_sr = sr_candidate * (1.0f + delta);
            float corr = fast_preamble_correlation(samples, num_samples, current_sr);

            if (i == 0 || (i < num_candidates && delta == 0.0f)) {
                printf("  [%d] SR=%.0f Hz (%.1f sps/chip): corr=%.3f",
                       i+1, sr_candidate, sr_candidate / DSSS_CHIP_RATE, corr);
            }

            if (corr > best_corr) {
                best_corr = corr;
                best_sr = current_sr;
                if (delta == 0.0f) printf(" ← NEW BEST");
            }

            if (i == 0 || (i < num_candidates && delta == 0.0f)) {
                printf("\n");
            }
        }
        valid_candidates++;
    }

    if (valid_candidates == 0) {
        fprintf(stderr, "Warning: No valid sample rate candidates for %zu samples\n", num_samples);
        fprintf(stderr, "Using fallback: 384000 Hz\n");
        return 384000.0f;
    }

    printf("\n✓ Sample rate estimated: %.0f Hz\n", best_sr);
    printf("  Best correlation: %.3f\n", best_corr);
    printf("  Samples per chip: %.2f\n", best_sr / DSSS_CHIP_RATE);
    printf("  Tested %d/%d candidates\n", valid_candidates, num_candidates);
    printf("=================================\n\n");

    return best_sr;
}

// =============================================================================
// PREAMBLE DETECTION
// =============================================================================

/**
 * @brief Generate DSSS preamble reference at chip rate
 *
 * Generates 6,400 chips (25 I bits + 25 Q bits × 256 chips/bit)
 * Preamble pattern: alternating 0,1,0,1,...
 *
 * This matches test_sample_rate.c which successfully detects preamble.
 *
 * @param preamble_chips Output buffer for chips (6,400 complex chips)
 * @return 0 on success
 */
static int generate_preamble_chips(float complex *preamble_chips) {
    prn_state_t prn_state_i, prn_state_q;
    prn_init(&prn_state_i, 0);  // Normal mode I-channel
    prn_init(&prn_state_q, 0);  // Normal mode Q-channel

    int8_t prn_i_buf[DSSS_SPREADING_FACTOR];
    int8_t prn_q_buf[DSSS_SPREADING_FACTOR];

    int chip_idx = 0;

    // Generate 25 bits per channel (I and Q = 50 bits total preamble)
    for (int bit = 0; bit < DSSS_PREAMBLE_LENGTH / 2; bit++) {
        // Generate PRN sequences for this bit
        prn_generate_i(&prn_state_i, prn_i_buf);
        prn_generate_q(&prn_state_q, prn_q_buf);

        // Preamble pattern: alternating 0,1,0,1...
        // Bit value affects sign of chips
        int bit_value_i = (bit * 2) % 2;        // Even positions: 0,1,0,1...
        int bit_value_q = (bit * 2 + 1) % 2;    // Odd positions: 1,0,1,0...

        // Spread each bit across 256 chips
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            // Convert PRN from {0,1} to {-1,+1} considering bit value
            // Bit 0: keep PRN sign, Bit 1: flip PRN sign
            float chip_i = (bit_value_i == 0) ? (float)prn_i_buf[c] : -(float)prn_i_buf[c];
            float chip_q = (bit_value_q == 0) ? (float)prn_q_buf[c] : -(float)prn_q_buf[c];

            preamble_chips[chip_idx++] = chip_i + I * chip_q;
        }
    }

    return 0;
}

/**
 * @brief Upsample preamble chips to match signal sample rate
 *
 * Uses zero-order hold (repeat each chip sps times)
 * This matches test_sample_rate.c upsampling method.
 *
 * @param preamble_chips Input chips at chip rate (6,400)
 * @param num_chips Number of chips (6,400)
 * @param preamble_samples Output upsampled preamble
 * @param sps Samples per chip (e.g., 65.1 for 2.5 MHz)
 * @return Number of output samples
 */
static int upsample_preamble(const float complex *preamble_chips, int num_chips,
                             float complex *preamble_samples, float sps) {
    int num_samples = (int)(num_chips * sps);

    for (int i = 0; i < num_samples; i++) {
        int chip_idx = (int)(i / sps);
        if (chip_idx >= num_chips) chip_idx = num_chips - 1;
        preamble_samples[i] = preamble_chips[chip_idx];
    }

    return num_samples;
}

/**
 * @brief Correlate two signals
 */
/**
 * @brief Compute normalized cross-correlation between two complex signals
 *
 * Uses standard complex correlation: |Σ(s1 * conj(s2))| / sqrt(power1 * power2)
 * This matches test_sample_rate.c normalization.
 *
 * @param sig1 First signal
 * @param sig2 Second signal
 * @param len Length of both signals
 * @return Normalized correlation [0.0, 1.0]
 */
static float correlate_signals(const float complex *sig1, const float complex *sig2,
                               size_t len) {
    float complex corr_sum = 0.0f;
    float power1 = 0.0f, power2 = 0.0f;

    for (size_t i = 0; i < len; i++) {
        corr_sum += sig1[i] * conjf(sig2[i]);
        power1 += cabsf(sig1[i]) * cabsf(sig1[i]);
        power2 += cabsf(sig2[i]) * cabsf(sig2[i]);
    }

    // Normalized correlation (like test_sample_rate.c)
    if (power1 > 0 && power2 > 0) {
        return cabsf(corr_sum) / sqrtf(power1 * power2);
    }
    return 0.0f;
}

int dsss_detect_preamble(const float complex *samples, size_t num_samples,
                         float samp_rate, int *preamble_idx,
                         float *freq_offset, float *correlation) {
    // Preamble detection parameters
    //const int preamble_offset = 200;  // Skip AGC settling

    // Calculate samples per chip (keep as float for accurate preamble length)
    float sps = samp_rate / DSSS_CHIP_RATE;  // e.g., 65.104166 for 2.5 MHz
    int sps_int = (int)(sps + 0.5f);         // Rounded for step size

    // Use FULL DSSS preamble for accurate detection (like test_sample_rate)
    // Preamble: 50 bits = 25 bits I + 25 bits Q
    // Each bit spread over 256 chips = 6,400 chips total
    const int num_preamble_chips = (DSSS_PREAMBLE_LENGTH / 2) * DSSS_SPREADING_FACTOR;  // 6,400
    const int preamble_length = (int)(num_preamble_chips * sps);  // Use float sps for accuracy!

    printf("  Preamble detection: %d chips × %.2f sps = %d samples (was: 500)\n",
           num_preamble_chips, sps, preamble_length);

    // Generate reference preamble chips at chip rate (like test_sample_rate.c)
    float complex *preamble_chips = calloc(num_preamble_chips, sizeof(float complex));
    if (!preamble_chips) return -1;

    generate_preamble_chips(preamble_chips);

    // Upsample QPSK chips to samples (like test_sample_rate.c)
    float complex *preamble_qpsk = calloc(preamble_length, sizeof(float complex));
    if (!preamble_qpsk) {
        free(preamble_chips);
        return -1;
    }
    upsample_preamble(preamble_chips, num_preamble_chips, preamble_qpsk, sps);
    free(preamble_chips);  // No longer needed

    // Apply OQPSK delay (Tc/2 on Q channel) to reference preamble
    // This matches test_sample_rate.c which correlates OQPSK signal with OQPSK reference
    float complex *preamble_ref = calloc(preamble_length, sizeof(float complex));
    if (!preamble_ref) {
        free(preamble_qpsk);
        return -1;
    }

    int delay = (int)(sps / 2.0f);  // Tc/2 delay in samples
    for (int i = 0; i < preamble_length; i++) {
        float i_val = crealf(preamble_qpsk[i]);
        float q_val = (i >= delay) ? cimagf(preamble_qpsk[i - delay]) : 0.0f;
        preamble_ref[i] = i_val + I * q_val;
    }
    free(preamble_qpsk);  // No longer needed

    // Search over frequency offsets (±12 kHz for LEO Doppler)
    float best_corr = 0.0f;
    int best_idx = -1;
    float best_freq = 0.0f;

    // Calculate number of frequencies to test
    const int num_freqs = (int)((2 * DSSS_MAX_DOPPLER) / DSSS_FREQ_SEARCH_STEP) + 1;  // 161
    int debug_count = 0;

    printf("  Frequency search (testing %d frequencies every %d Hz, range ±%d Hz):\n",
           num_freqs, DSSS_FREQ_SEARCH_STEP, DSSS_MAX_DOPPLER);
    printf("  [OPENMP] Parallelizing frequency search across %d cores...\n", omp_get_max_threads());

    // Parallelize frequency search - 161 frequencies distributed to all cores
    #pragma omp parallel for schedule(dynamic, 4)
    for (int f_idx = 0; f_idx < num_freqs; f_idx++) {
        float f_offset = -DSSS_MAX_DOPPLER + f_idx * DSSS_FREQ_SEARCH_STEP;

        // Thread-local buffers
        float complex *preamble_shifted = calloc(preamble_length, sizeof(float complex));
        if (!preamble_shifted) continue;

        // Apply frequency offset to reference
        for (int i = 0; i < preamble_length; i++) {
            float t = (float)i / samp_rate;
            preamble_shifted[i] = preamble_ref[i] * cexpf(I * 2.0f * M_PI * f_offset * t);
        }

        // Correlate with received signal (search first 50% of buffer where preamble should be)
        size_t search_length = num_samples / 2;  // Search first 50%
        float max_corr_this_freq = 0.0f;
        int max_idx_this_freq = -1;

        for (size_t idx = 0; idx < search_length - preamble_length; idx += sps_int) {
            // Correlate OQPSK signal with OQPSK reference (like test_sample_rate)
            float corr = correlate_signals(&samples[idx],
                                          preamble_shifted, preamble_length);

            if (corr > max_corr_this_freq) {
                max_corr_this_freq = corr;
                max_idx_this_freq = idx;
            }
        }

        free(preamble_shifted);

        // Thread-safe update of global best
        #pragma omp critical
        {
            if (max_corr_this_freq > best_corr) {
                best_corr = max_corr_this_freq;
                best_idx = max_idx_this_freq;
                best_freq = f_offset;
            }

            // Debug output for interesting frequencies (limited to avoid spam)
            if (debug_count < 10 ||
                fabs(f_offset) < 500 ||  // Around 0 Hz
                max_corr_this_freq > 0.7) {  // High correlation
                printf("    f=%+7.0f Hz: corr=%.3f at idx=%d\n",
                       f_offset, max_corr_this_freq, max_idx_this_freq);
                debug_count++;
            }
        }
    }

    printf("  Completed frequency search: tested %d frequencies\n",
           (int)((2 * DSSS_MAX_DOPPLER) / DSSS_FREQ_SEARCH_STEP) + 1);

    free(preamble_ref);

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
/*static void calculate_loop_gains(float loop_bw, float damping, float *k1, float *k2) {
    float zeta = damping;
    float omega_n = loop_bw;
    float denom = 1.0f + 2.0f * zeta * omega_n + omega_n * omega_n;

    *k1 = (4.0f * zeta * omega_n) / denom;
    *k2 = (4.0f * omega_n * omega_n) / denom;
}*/

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
/*static float gardner_ted(float complex early, float complex prompt, float complex late) {
    return crealf((late - early) * conjf(prompt));
}*/

/**
 * @brief Modified Gardner TED avec échantillons à T/2 - OPTION AMÉLIORÉE
 * 
 * Utilise des échantillons à T/2 d'intervalle pour meilleure précision
 */
/*static float gardner_ted_improved(float complex prev_sample, float complex curr_sample, 
                                 float complex next_sample, int sps) {
    // Échantillons à T/2 d'intervalle
    float complex at_minus_half = prev_sample;  // n-1
    float complex at_zero = curr_sample;        // n (point idéal)
    float complex at_plus_half = next_sample;   // n+1
    
    return crealf((at_plus_half - at_minus_half) * conjf(at_zero));
}*/

/**
 * @brief Version alternative avec paramètres éprouvés
 */
/*static void calculate_timing_loop_gains_safe(float *k1, float *k2) {
    // Paramètres conservateurs testés empiriquement
    // pour signal DSSS avec spreading factor 256
    *k1 = 0.0001f;  // Gain proportionnel très faible
    *k2 = 0.00001f; // Gain intégral encore plus faible
}*/

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

/**
 * @brief Calcul des gains de boucle CORRECT pour synchronisation symbole
 */
static void calculate_symbol_sync_gains(float loop_bw, float damping, float *k1, float *k2) {
    float omega_n = loop_bw;
    float zeta = damping;
    float denom = 1.0f + 2.0f * zeta * omega_n + omega_n * omega_n;
    
    *k1 = (2.0f * zeta * omega_n) / denom;
    *k2 = (omega_n * omega_n) / denom;
    
    // Ajustement pour signal DSSS
    *k1 *= 0.5f;
    *k2 *= 0.25f;
}

/**
 * @brief Gardner Timing Error Detector - IMPLÉMENTATION CORRECTE
 */
static float gardner_ted_corrected(float complex early, float complex prompt, float complex late) {
    float complex diff = late - early;
    return crealf(diff * conjf(prompt));
}

/**
 * @brief Timing Recovery CORRIGÉE pour signal DSSS/OQPSK
 */
int dsss_timing_recovery_corrected(const float complex *input, float complex *output,
                                  size_t num_samples, size_t *num_symbols, float samp_rate) {
    
    // Paramètres optimisés pour DSSS
    const float damping = 1.0f;
    const float loop_bw = 0.0005f;
    
    // === Calcul incrément de phase ===
    float samples_per_chip = samp_rate / DSSS_CHIP_RATE;  // ~65.104
    float phase_increment = samples_per_chip;      // 65.104
    int sps = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);   // Garder pour debug
    
    // Calcul des gains
    float k1, k2;
    calculate_symbol_sync_gains(loop_bw, damping, &k1, &k2);
    
    printf("[TIMING CORRIGÉ] Paramètres: bw=%.6f, damping=%.2f, sps=%d\n", loop_bw, damping, sps);
    printf("[TIMING CORRIGÉ] VRAI phase_increment=%.8f (1/%.6f)\n", phase_increment, samples_per_chip);
    printf("[TIMING CORRIGÉ] Gains: k1=%.8f, k2=%.8f\n", k1, k2);

    // État de la boucle
    //float timing_phase = 2.0f;
    float timing_phase = 1.0f;
    float timing_freq = 0.0f;
    size_t symbol_count = 0;
    
    // Historique pour Gardner TED
    float complex prev_prev_sym = 0.0f;
    float complex prev_sym = 0.0f;
    float complex curr_sym = 0.0f;
    
    // BOUCLE PRINCIPALE
    while (timing_phase < num_samples - 4 && symbol_count < 38400) {
        
        // Extraction du symbole avec interpolation cubique
        int center_idx = (int)floorf(timing_phase);
        float mu = timing_phase - center_idx;
        
        if (center_idx < 1 || center_idx + 2 >= (int)num_samples) {
            break;
        }
        
        float complex symbol = interpolate_cubic(input, mu, center_idx, num_samples);
        output[symbol_count] = symbol;
        
        // Debug des premiers échantillons
        if (symbol_count < 5) {
            printf("[TIMING DEBUG] symbol[%zu]: center_idx=%d (phase=%.3f, mu=%.3f)\n",
                symbol_count, center_idx, timing_phase, mu);
            printf("[TIMING DEBUG]   input[%d]=%.3f+j%.3f → output=%.3f+j%.3f\n",
                center_idx, crealf(input[center_idx]), cimagf(input[center_idx]),
                crealf(symbol), cimagf(symbol));
        }
        
        // Mise à jour de l'historique pour Gardner TED
        prev_prev_sym = prev_sym;
        prev_sym = curr_sym; 
        curr_sym = symbol;
        
        // Calcul erreur de timing (Gardner TED)
        float timing_error = 0.0f;
        if (symbol_count >= 2) {
            timing_error = gardner_ted_corrected(prev_prev_sym, prev_sym, curr_sym);
            
            // Limiter l'erreur
            if (timing_error > 0.5f) timing_error = 0.5f;
            if (timing_error < -0.5f) timing_error = -0.5f;
        }
        
        // === CORRECTION 2 : Utiliser phase_increment au lieu de sps ===
        if (symbol_count >= 2) {
            timing_freq += k2 * timing_error;
            timing_phase += phase_increment + k1 * timing_error + timing_freq;  // ← CHANGÉ
        } else {
            timing_phase += phase_increment;  // ← CHANGÉ
        }
        
        symbol_count++;
        
        // Debug périodique
        if (symbol_count % 5000 == 0) {
            printf("[TIMING] Symbole %zu: phase=%.2f, mu=%.3f, center_idx=%d\n",
                   symbol_count, timing_phase, mu, (int)floorf(timing_phase));
        }
    }
    
    // Statistiques finales
    printf("[TIMING CORRIGÉ] Récupération terminée: %zu symboles\n", symbol_count);
    printf("[TIMING CORRIGÉ] Phase finale: %.2f, Fréquence NCO: %.6f\n", 
           timing_phase, timing_freq);
    
    if (num_symbols) *num_symbols = symbol_count;
    
    // VALIDATION
    if (symbol_count < 36000) {
        fprintf(stderr, "[TIMING CORRIGÉ] ERREUR: Symboles insuffisants (%zu < 36000)\n", symbol_count);
        return -1;
    }
    
    return 0;
}

/**
 * @brief Test simple de validation du timing recovery
 */
int test_timing_recovery_simple() {
    printf("=== TEST TIMING RECOVERY SIMPLE ===\n");
    
    // Créer un signal de test simple (sinusoïde)
    const size_t TEST_LENGTH = 100000;
    float complex *test_signal = malloc(TEST_LENGTH * sizeof(float complex));
    
    if (!test_signal) return -1;
    
    // Générer une sinusoïde complexe
    for (size_t i = 0; i < TEST_LENGTH; i++) {
        float t = (float)i / 1000.0f;
        test_signal[i] = cexpf(I * 2.0f * M_PI * t);
    }
    
    float complex *output_symbols = malloc(40000 * sizeof(float complex));
    size_t num_recovered = 0;
    
    // Tester le timing recovery
    int result = dsss_timing_recovery_corrected(test_signal, output_symbols, 
                                               TEST_LENGTH, &num_recovered, 2.5e6f);
    
    printf("Test résultat: %s, %zu symboles récupérés\n", 
           (result == 0) ? "SUCCÈS" : "ÉCHEC", num_recovered);
    
    free(test_signal);
    free(output_symbols);
    return result;
}

/**
 * @brief Validation de l'interpolation cubique
 */
void validate_cubic_interpolation() {
    printf("=== VALIDATION INTERPOLATION CUBIQUE ===\n");
    
    // Signal test: sinusoïde pure
    float complex test_signal[100];
    for (int i = 0; i < 100; i++) {
        test_signal[i] = cexpf(I * 2.0f * M_PI * i / 10.0f);
    }
    
    // Tester différentes positions fractionnaires
    for (float mu = 0.0f; mu <= 1.0f; mu += 0.1f) {
        float complex interpolated = interpolate_cubic(test_signal, mu, 10, 100);
        float complex expected = cexpf(I * 2.0f * M_PI * (10.0f + mu) / 10.0f);
        
        float error = cabsf(interpolated - expected);
        printf("  mu=%.1f: erreur=%.6f %s\n", mu, error, 
               (error < 0.01f) ? "✅" : "❌");
    }
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
int dsss_resolve_phase_ambiguity(float complex *symbols, size_t num_symbols,
                                 int *phase_rot, bool *iq_swap,
                                 dsss_demod_state_t *state) {
    printf("[PHASE] Fine rotation search for optimal parameters...\n");
    printf("[PHASE] Pre-generating PRN sequences for preamble (50 bits)...\n");
    
    // Générer les séquences PRN pour le preamble (50 bits = 25 I + 25 Q)
    prn_state_t prn_preamble_i, prn_preamble_q;
    prn_init(&prn_preamble_i, 0);
    prn_init(&prn_preamble_q, 0);

    int8_t *prn_preamble_i_full = malloc(25 * DSSS_SPREADING_FACTOR * sizeof(int8_t));
    int8_t *prn_preamble_q_full = malloc(25 * DSSS_SPREADING_FACTOR * sizeof(int8_t));

    if (!prn_preamble_i_full || !prn_preamble_q_full) {
        fprintf(stderr, "[PHASE] Error: malloc failed for PRN preamble buffers\n");
        free(prn_preamble_i_full);
        free(prn_preamble_q_full);
        return -1;
    }

    int8_t prn_chunk[DSSS_SPREADING_FACTOR];
    for (int bit = 0; bit < 25; bit++) {
        prn_generate_i(&prn_preamble_i, prn_chunk);
        memcpy(&prn_preamble_i_full[bit * DSSS_SPREADING_FACTOR], prn_chunk, DSSS_SPREADING_FACTOR * sizeof(int8_t));
        
        prn_generate_q(&prn_preamble_q, prn_chunk);
        memcpy(&prn_preamble_q_full[bit * DSSS_SPREADING_FACTOR], prn_chunk, DSSS_SPREADING_FACTOR * sizeof(int8_t));
    }
    
    printf("[PHASE] PRN preamble sequences generated (%d chips I, %d chips Q)\n", 
           25 * DSSS_SPREADING_FACTOR, 25 * DSSS_SPREADING_FACTOR);
    
    printf("[PHASE] Phase 1: Testing 1440 combinations (360°×2 swaps×2 inversions)...\n");

    // T.018 §2.2.4: Expected preamble = all bits '0' (50 bits)
    /*uint8_t expected_preamble[DSSS_PREAMBLE_LENGTH];
    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
        expected_preamble[i] = 0;  // All preamble bits = 0
    }*/

    // Number of chips for preamble (50 bits × 256 chips/bit = 12,800 chips)
    const int preamble_chips = DSSS_PREAMBLE_LENGTH * DSSS_SPREADING_FACTOR;

    // Check we have enough symbols
    if (num_symbols < preamble_chips) {
        fprintf(stderr, "[PHASE] Error: Not enough symbols for preamble (%zu < %d)\n",
                num_symbols, preamble_chips);
        
        free(prn_preamble_i_full);
        free(prn_preamble_q_full);
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

    // PHASE 1: Test 1440 phase/swap/invert combinations on RAW SYMBOLS
    // (Like test_fine_phase.c - test on symbols BEFORE despreading)
    float best_phase1_corr = 0.0f;
    float best_angle_deg = 0.0f;
    bool best_swap = false;
    bool best_invert = false;

    const int total_phase1 = 360 * 2 * 2;  // 1440 combinations
    int progress = 0;

    // Test on first 50 bits = 12,800 chips (preamble)
    // T.018: 50 bits × 256 chips/bit = 12,800 chips total
    const int test_symbols = DSSS_PREAMBLE_LENGTH * DSSS_SPREADING_FACTOR;  // 12,800
    // Vérifier que nous avons assez de symboles
    if (num_symbols < test_symbols) {
        fprintf(stderr, "[PHASE] Error: Not enough symbols for preamble testing (%zu < %d)\n",
                num_symbols, test_symbols);
        free(prn_preamble_i_full);
        free(prn_preamble_q_full);
        return -1;
    }

    printf("  [OPENMP] Parallelizing Phase 1 (%d combinations) across %d cores...\n",
           total_phase1, omp_get_max_threads());

    #pragma omp parallel for schedule(dynamic, 8)
    for (int combo = 0; combo < total_phase1; combo++) {
    // Decode combo into (angle, swap, invert)
    int invert = combo % 2;
    int swap = (combo / 2) % 2;
    int angle = combo / 4;

    float angle_rad = angle * M_PI / 180.0f;
    float complex rot = cexpf(I * angle_rad);

    // Appliquer transformation aux symboles du preamble
    float complex *test_symbols_transformed = malloc(test_symbols * sizeof(float complex));
    if (!test_symbols_transformed) continue;
    
    for (int i = 0; i < test_symbols && i < num_symbols; i++) {
        test_symbols_transformed[i] = symbols[i] * rot;
        if (swap) {
            test_symbols_transformed[i] = cimagf(test_symbols_transformed[i]) + 
                                         I * crealf(test_symbols_transformed[i]);
        }
    }

    // Convertir symboles en chips (avec inversion)
    uint8_t *chips_i_test = malloc(test_symbols);
    uint8_t *chips_q_test = malloc(test_symbols);
    
    if (!chips_i_test || !chips_q_test) {
        free(test_symbols_transformed);
        free(chips_i_test);
        free(chips_q_test);
        continue;
    }
    
    for (int i = 0; i < test_symbols && i < num_symbols; i++) {
        chips_i_test[i] = (crealf(test_symbols_transformed[i]) >= 0) ? 
                         (invert ? 1 : 0) : (invert ? 0 : 1);
        chips_q_test[i] = (cimagf(test_symbols_transformed[i]) >= 0) ? 
                         (invert ? 1 : 0) : (invert ? 0 : 1);
    }

    // DÉSÉTALER et comparer avec le pattern 010101...
    int matches = 0;
    int total_bits_tested = 0;

    // Désétaler les 50 bits du preamble (25 bits I + 25 bits Q)
    for (int bit = 0; bit < 25; bit++) {
        int start_idx = bit * DSSS_SPREADING_FACTOR;
        int corr_i = 0, corr_q = 0;

        // Désétaler canal I
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            int chip_idx = start_idx + c;
            if (chip_idx < test_symbols) {
                uint8_t rx_chip_i = chips_i_test[chip_idx];
                uint8_t ref_i = (prn_preamble_i_full[chip_idx] == -1) ? 1 : 0;
                if (rx_chip_i == ref_i) corr_i++;
            }
        }

        // Désétaler canal Q
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            int chip_idx = start_idx + c;
            if (chip_idx < test_symbols) {
                uint8_t rx_chip_q = chips_q_test[chip_idx];
                uint8_t ref_q = (prn_preamble_q_full[chip_idx] == -1) ? 1 : 0;
                if (rx_chip_q == ref_q) corr_q++;
            }
        }

        // Décision majoritaire (seuil à 50%)
        uint8_t bit_i = (corr_i > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;
        uint8_t bit_q = (corr_q > DSSS_SPREADING_FACTOR / 2) ? 0 : 1;

        // Pattern attendu du preamble: I=010101..., Q=101010...
        uint8_t expected_i = (2 * bit) % 2;      // 0,1,0,1,0,1...
        uint8_t expected_q = (2 * bit + 1) % 2;  // 1,0,1,0,1,0...

        if (bit_i == expected_i) matches++;
        if (bit_q == expected_q) matches++;
        total_bits_tested += 2;
    }

    float corr = (total_bits_tested > 0) ? (float)matches / total_bits_tested : 0.0f;

    // Nettoyer la mémoire
    free(test_symbols_transformed);
    free(chips_i_test);
    free(chips_q_test);

    // Mise à jour thread-safe des meilleures valeurs
    #pragma omp critical
    {
        if (corr > best_phase1_corr) {
            best_phase1_corr = corr;
            best_angle_deg = (float)angle;
            best_swap = (bool)swap;
            best_invert = (bool)invert;
        }

        // Debug pour les bonnes corrélations
        if (corr > 0.8f) {
            printf("[PHASE STATS] angle=%d°, swap=%d, invert=%d: corr=%.1f%%",
                   angle, swap, invert, corr * 100.0f);
            
            if (angle == 0 && swap == 0 && invert == 0) printf(" ← 0° REFERENCE");
            if (angle == 0 && swap == 0 && invert == 1) printf(" ← 0° WITH INVERT");
            if (angle == 180 && swap == 0 && invert == 1) printf(" ← 180° EQUIVALENT");
            if (angle == 45 && swap == 0 && invert == 0) printf(" ← 45° (OLD ALGO CHOICE)");
            printf("\n");
        }

        // Indicateur de progression
        progress++;
        if (progress % 72 == 0) {  // Tous les 5%
            printf("  Progress: %d%% (best: %.1f%%)\r",
                   (progress * 100) / total_phase1, best_phase1_corr * 100.0f);
            fflush(stdout);
        }
    }
}

    printf("\n");

    printf("[PHASE] Phase 1 complete: angle=%.0f°, swap=%d, invert=%d, corr=%.1f%%\n",
           best_angle_deg, best_swap, best_invert, best_phase1_corr * 100.0f);

    // Apply Phase 1 corrections to ALL symbols (not just preamble)
    printf("[PHASE] Applying phase corrections to symbols...\n");
    float best_angle_rad = best_angle_deg * M_PI / 180.0f;
    float complex rot = cexpf(I * best_angle_rad);

    for (size_t i = 0; i < num_symbols; i++) {
        symbols[i] *= rot;
        if (best_swap) {
            symbols[i] = cimagf(symbols[i]) + I * crealf(symbols[i]);
        }
    }
    
    printf("[PHASE DEBUG] Top 5 phase combinations:\n");

    // PHASE 2: Extended chip offset search on corrected symbols
    // Testing wider range: -15 to +15 chips (was -5 to +5)
    // Testing more bits: 75 bits per channel (was 25)
    const int test_bits_phase2 = 25;  // Reduced to 25 for fast testing (was 75)
    const int test_chips_phase2 = test_bits_phase2 * DSSS_SPREADING_FACTOR;

    printf("[PHASE] Phase 2: Extended chip offset search (-15 to +15, %d bits)...\n", test_bits_phase2 * 2);

    // Default despreading parameters for Phase 2
    const int default_chip_conv = 0;  // Real>0 → 0
    const int default_prn_conv = 0;   // -1 → 1, +1 → 0
    const int default_interleave = 0; // I,Q,I,Q
    const int default_offset = 0;     // Changed from -1 after preamble fix

    float best_corr = best_phase1_corr;
    int best_chip_conv = default_chip_conv;
    int best_prn_conv = default_prn_conv;
    int best_interleave = default_interleave;
    int best_offset = default_offset;

    // Demodulate chips with corrected symbols + inversion (extended range)
    uint8_t *chips_i = malloc(test_chips_phase2);
    uint8_t *chips_q = malloc(test_chips_phase2);

    if (!chips_i || !chips_q) {
        free(chips_i); free(chips_q);
        free(prn_i_signed); free(prn_q_signed);
        free(prn_i_full); free(prn_q_full);
        return -1;
    }

    // Convert symbols to chips with inversion from Phase 1
    for (int i = 0; i < test_chips_phase2 && i < num_symbols; i++) {
        float complex test_sym = symbols[i];
        chips_i[i] = (crealf(test_sym) >= 0) ? (best_invert ? 1 : 0) : (best_invert ? 0 : 1);
        chips_q[i] = (cimagf(test_sym) >= 0) ? (best_invert ? 1 : 0) : (best_invert ? 0 : 1); 
    }
    printf("[DEBUG] Phase resolution: symbols[0 .. %d] → chips[0 .. %d] (1:1 mapping)\n",
           test_chips_phase2 - 1, test_chips_phase2 - 1);

    // Regenerate PRN for extended test (was prn_i_full/prn_q_full - now using variations)
    free(prn_i_signed);
    free(prn_q_signed);

    prn_i_signed = malloc(test_bits_phase2 * DSSS_SPREADING_FACTOR);
    prn_q_signed = malloc(test_bits_phase2 * DSSS_SPREADING_FACTOR);

    if (!prn_i_signed || !prn_q_signed) {
        free(prn_i_signed); free(prn_q_signed);
        free(chips_i); free(chips_q);
        return -1;
    }

    // Generate extended PRN sequences
    prn_init(&prn_state, 0);
    for (int bit = 0; bit < test_bits_phase2; bit++) {
        prn_generate_i(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            prn_i_signed[bit * DSSS_SPREADING_FACTOR + c] = prn_chunk_signed[c];
        }
    }

    prn_init(&prn_state, 0);
    for (int bit = 0; bit < test_bits_phase2; bit++) {
        prn_generate_q(&prn_state, prn_chunk_signed);
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            prn_q_signed[bit * DSSS_SPREADING_FACTOR + c] = prn_chunk_signed[c];
        }
    }

    // Precalculate all 4 PRN variations (2 chip_conv × 2 prn_conv)
    // This avoids regenerating PRN in parallel threads
    uint8_t *prn_variations[4][2];  // [variation][I=0/Q=1]
    for (int v = 0; v < 4; v++) {
        prn_variations[v][0] = malloc(test_bits_phase2 * DSSS_SPREADING_FACTOR);
        prn_variations[v][1] = malloc(test_bits_phase2 * DSSS_SPREADING_FACTOR);
        if (!prn_variations[v][0] || !prn_variations[v][1]) {
            fprintf(stderr, "Error: malloc failed for PRN variations\n");
            for (int i = 0; i < v; i++) {
                free(prn_variations[i][0]);
                free(prn_variations[i][1]);
            }
            free(chips_i); free(chips_q);
            free(prn_i_signed); free(prn_q_signed);
            return -1;
        }
    }

    // Generate all 4 variations: v=0(cc0,pc0), v=1(cc0,pc1), v=2(cc1,pc0), v=3(cc1,pc1)
    for (int chip_conv = 0; chip_conv < 2; chip_conv++) {
        for (int prn_conv = 0; prn_conv < 2; prn_conv++) {
            int v = chip_conv * 2 + prn_conv;
            for (int c = 0; c < test_bits_phase2 * DSSS_SPREADING_FACTOR; c++) {
                prn_variations[v][0][c] = (prn_conv == 0) ?
                    ((prn_i_signed[c] == -1) ? 1 : 0) :
                    ((prn_i_signed[c] == -1) ? 0 : 1);
                prn_variations[v][1][c] = (prn_conv == 0) ?
                    ((prn_q_signed[c] == -1) ? 1 : 0) :
                    ((prn_q_signed[c] == -1) ? 0 : 1);
            }
        }
    }

    // Flatten all loops into single iteration space: 248 combinations
    // Each combo_id encodes: chip_conv, prn_conv, interleave, offset
    const int total_phase2 = 2 * 2 * 2 * 31;  // 248 combinations
    int progress_phase2 = 0;

    printf("[OPENMP] Parallelizing %d combinations across available cores...\n", total_phase2);

    // Parallelize the flattened loop - 248 iterations distributed to all cores
    #pragma omp parallel for schedule(dynamic, 4)
    for (int combo_id = 0; combo_id < total_phase2; combo_id++) {
        // Decode combo_id into parameters
        int offset = (combo_id % 31) - 15;             // -15..+15
        int interleave = (combo_id / 31) % 2;          // 0 or 1
        int prn_conv = (combo_id / 62) % 2;            // 0 or 1
        int chip_conv = (combo_id / 124) % 2;          // 0 or 1

        // Select appropriate PRN variation
        int variation = chip_conv * 2 + prn_conv;
        uint8_t *prn_i = prn_variations[variation][0];
        uint8_t *prn_q = prn_variations[variation][1];

        // Thread-local despread buffer
        uint8_t despread_bits[test_bits_phase2 * 2];

        // Despread using this parameter combination
        for (int bit = 0; bit < test_bits_phase2; bit++) {
            int start_idx = bit * DSSS_SPREADING_FACTOR;
            int corr_i = 0, corr_q = 0;

            for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
                int chip_idx = start_idx + c + offset;
                if (chip_idx >= 0 && chip_idx < test_chips_phase2) {
                    uint8_t recv_chip_i = chip_conv ? (1 - chips_i[chip_idx]) : chips_i[chip_idx];
                    uint8_t recv_chip_q = chip_conv ? (1 - chips_q[chip_idx]) : chips_q[chip_idx];

                    if (recv_chip_i == prn_i[start_idx + c]) corr_i++;
                    if (recv_chip_q == prn_q[start_idx + c]) corr_q++;
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

        // Compare with expected pattern (alternating 0101...)
        int matches = 0;
        int total_test_bits = test_bits_phase2 * 2;
        for (int i = 0; i < total_test_bits; i++) {
            uint8_t expected_bit = i % 2;
            if (despread_bits[i] == expected_bit) matches++;
        }
        float corr = (float)matches / total_test_bits;

        // Thread-safe update of best results
        #pragma omp critical
        {
            if (corr > best_corr) {
                best_corr = corr;
                best_chip_conv = chip_conv;
                best_prn_conv = prn_conv;
                best_interleave = interleave;
                best_offset = offset;
                printf("[PHASE]   NEW BEST: corr=%.1f%%, chip_conv=%d, prn_conv=%d, interleave=%d, offset=%+d\n",
                       corr * 100.0f, chip_conv, prn_conv, interleave, offset);
            }

            // Progress indicator
            progress_phase2++;
            if (progress_phase2 % 25 == 0) {
                printf("  Phase 2 progress: %d%% (best: %.1f%%)\r",
                       (progress_phase2 * 100) / total_phase2, best_corr * 100.0f);
                fflush(stdout);
            }
        }
    }
    
    // AJOUTEZ cette section après la boucle Phase 1 :
    printf("[PHASE TOP] Top combinations analysis:\n");
    printf("[PHASE TOP] Best found by algorithm: angle=%.0f°, swap=%d, invert=%d (corr=%.1f%%)\n",
        best_angle_deg, best_swap, best_invert, best_phase1_corr * 100.0f);

    // Afficher les performances des angles clés
    printf("[PHASE TOP] Key angles performance:\n");
    // Vous pouvez ajouter manuellement les angles que vous voulez surveiller
    printf("[PHASE TOP] - 0° configs: swap=0/invert=1 (empirical best), swap=1/invert=0, etc.\n");
    printf("[PHASE TOP] - 45° configs: current algorithm choice\n");
    printf("[PHASE TOP] - 90° configs: common ambiguity\n");
    printf("[PHASE TOP] - 180° configs: equivalent to 0° with invert\n");

    // Free PRN variations
    for (int v = 0; v < 4; v++) {
        free(prn_variations[v][0]);
        free(prn_variations[v][1]);
    }
    printf("\n");  // Clear progress line

    free(chips_i);
    free(chips_q);
    free(prn_i_signed);
    free(prn_q_signed);
    free(prn_i_full);
    free(prn_q_full);

    printf("[PHASE] ✓ FINAL: angle=%.0f°, swap=%d, invert=%d, corr=%.1f%%\n",
           best_angle_deg, best_swap, best_invert, best_corr * 100.0f);
    printf("[PHASE] ✓ Despread params: chip_conv=%d, prn_conv=%d, interleave=%d, offset=%+d\n",
           best_chip_conv, best_prn_conv, best_interleave, best_offset);
    
    printf("=== CHIPS AFTER PHASE RESOLUTION ===\n");
    for (int i = 0; i < 10; i++) {
        printf("symbol[%d]: real=%.3f → chip_i=%d, imag=%.3f → chip_q=%d\n",
            i, crealf(symbols[i]), 
            (crealf(symbols[i]) >= 0) ? 0 : 1,
            cimagf(symbols[i]),
            (cimagf(symbols[i]) >= 0) ? 0 : 1);
    }

    // Convert angle to legacy phase_rot (0-3) for backward compatibility
    int legacy_phase_rot = (int)(best_angle_deg / 90.0f) % 4;
    if (phase_rot) *phase_rot = legacy_phase_rot;
    if (iq_swap) *iq_swap = best_swap;

    // Store optimal parameters in state
    if (state) {
        
           // 🔍 DEBUG: Afficher l'angle trouvé par l'algorithme vs l'angle forcé
    printf("[PHASE DEBUG] Algorithm found: angle=%.0f°, swap=%d, invert=%d, corr=%.1f%%\n",
        best_angle_deg, best_swap, best_invert, best_corr * 100.0f);
        state->phase_angle_deg = best_angle_deg;
        state->iq_swapped = best_swap;
        state->bit_invert = best_invert;
        state->chip_convention = best_chip_conv;
        state->prn_conversion = best_prn_conv;
        state->interleaving = best_interleave;
        state->chip_offset = best_offset;
    }

    // Return success if correlation > 90% (strong match on preamble)
    if (best_corr > 0.7f) {
        return 0;
    } else if (best_corr > 0.6f) {
        fprintf(stderr, "[PHASE] Warning: Moderate correlation (%.1f%%), proceeding anyway\n",
                best_corr * 100.0f);
        return 0;
    } else {
        fprintf(stderr, "[PHASE] Error: Low correlation (%.1f%%), phase resolution failed\n",
                best_corr * 100.0f);
        
    free(prn_preamble_i_full);
    free(prn_preamble_q_full);
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
 * @param out_chip_conv Output: best chip convention
 * @param out_prn_conv  Output: best PRN conversion
 * @param out_interleave Output: best interleaving
 * @param out_offset    Output: best chip offset
 * @return Best correlation found (0.0 to 1.0)
 */
static float dsss_diagnose_despreading(const uint8_t *chips_i, const uint8_t *chips_q,
                                       size_t num_chips,
                                       int *out_chip_conv, int *out_prn_conv,
                                       int *out_interleave, int *out_offset) {
    printf("\n[DIAGNOSTIC] Testing DSSS despreading parameters...\n");
    printf("[DIAGNOSTIC] Testing preamble only (%zu chips = 50 bits)\n", num_chips);

    // Expected preamble pattern (T.018 §2.2.4: all bits '0')
    uint8_t expected[DSSS_PREAMBLE_LENGTH];
    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
        expected[i] = 0;  // All preamble bits = 0
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

    // Save optimal parameters to output pointers
    if (out_chip_conv) *out_chip_conv = best_chip_conv;
    if (out_prn_conv) *out_prn_conv = best_prn_conv;
    if (out_interleave) *out_interleave = best_interleave;
    if (out_offset) *out_offset = best_offset;

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

    printf("[DEBUG] Despreading: chips_i/q[0 .. 38399] with offset=%d → bits[0 .. 299]\n",
           chip_offset);
    printf("[DEBUG] Despread params: chip_conv=%d, prn_conv=%d, interleave=%d, offset=%d\n",
           chip_conv, prn_conv, interleave, chip_offset);

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
                uint8_t rx_chip_i = chips_i[chip_idx];
                uint8_t ref_i = prn_i_full[start_idx + c];

                // Apply chip convention (same logic as diagnostic)
                if (chip_conv == 0) {
                    if (rx_chip_i == ref_i) corr_i++;
                } else {
                    if (rx_chip_i != ref_i) corr_i++;
                }
            }
        }

        // Despread Q-channel with optimal parameters
        int corr_q = 0;
        for (int c = 0; c < DSSS_SPREADING_FACTOR; c++) {
            int chip_idx = start_idx + c + chip_offset;
            if (chip_idx >= 0 && chip_idx < 38400) {
                uint8_t rx_chip_q = chips_q[chip_idx];
                uint8_t ref_q = prn_q_full[start_idx + c];

                // Apply chip convention (same logic as diagnostic)
                if (chip_conv == 0) {
                    if (rx_chip_q == ref_q) corr_q++;
                } else {
                    if (rx_chip_q != ref_q) corr_q++;
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

        // Track correlation (asymmetric metric - penalize inversions)
        // corr > 128 → positive correlation → (corr - 128) / 128 ∈ [0, 1]
        // corr < 128 → negative correlation → 0 (bad)
        float norm_corr_i = fmaxf(0.0f, ((float)corr_i - DSSS_SPREADING_FACTOR / 2) /
                                        (DSSS_SPREADING_FACTOR / 2));
        float norm_corr_q = fmaxf(0.0f, ((float)corr_q - DSSS_SPREADING_FACTOR / 2) /
                                        (DSSS_SPREADING_FACTOR / 2));
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
                    dsss_demod_state_t *state, float forced_freq) {

    if (!iq_samples || !output_bits) {
        fprintf(stderr, "Error: NULL input\n");
        return -1;
    }

    // Initialize state
    dsss_demod_state_t local_state = {0};
    
    // === CORRECTION AMPLITUDE POUR perfect_interpolated.iq ===
    const float complex *processed_iq = iq_samples;
    float complex *amplified_iq = NULL;
    
    float max_val = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float real = crealf(iq_samples[i]);
        float imag = cimagf(iq_samples[i]);
        if (fabsf(real) > max_val) max_val = fabsf(real);
        if (fabsf(imag) > max_val) max_val = fabsf(imag);
    }

    if (max_val < 0.5f && max_val > 0.0f) {
        float scale = 1.0f / max_val;
        fprintf(stderr, "[FIX] Amplifying weak signal by %.1fx (max=%.3f)\n", scale, max_val);
        
        amplified_iq = malloc(num_samples * sizeof(float complex));
        if (!amplified_iq) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            return -1;
        }
        
        for (size_t i = 0; i < num_samples; i++) {
            amplified_iq[i] = iq_samples[i] * scale;
        }
        processed_iq = amplified_iq;
    }
    // === FIN CORRECTION ===

    // Step 1: AGC
    printf("Step 1: AGC...\n");
    float complex *agc_out = malloc(num_samples * sizeof(float complex));
    if (!agc_out) {
        if (amplified_iq) free(amplified_iq);
        return -1;
    }

    // Utiliser processed_iq au lieu de iq_samples
    dsss_agc(processed_iq, agc_out, num_samples, &local_state.agc_gain);
    printf("  AGC gain: %.2f\n", local_state.agc_gain);
    printf("[DEBUG] AGC: Input samples 0-%zu, output aligned 1:1 (no delay)\n", num_samples - 1);

    // Step 2: Preamble detection (skip if frequency is forced)
    printf("Step 2: Preamble detection...\n");
    int preamble_idx;
    float freq_offset_coarse;

    if (!isnan(forced_freq)) {
        // Skip frequency search, use forced offset
        printf("  Using forced frequency offset: %.1f Hz (skipping search)\n", forced_freq);
        freq_offset_coarse = forced_freq;
        preamble_idx = 0;
        local_state.correlation_peak = 1.0f;
    } else {
        // Normal frequency search - utiliser processed_iq
        if (dsss_detect_preamble(processed_iq, num_samples, samp_rate,
                                &preamble_idx, &freq_offset_coarse,
                                &local_state.correlation_peak) < 0) {
            free(agc_out);
            if (amplified_iq) free(amplified_iq);
            if (state) *state = local_state;
            return -2;
        }
    }

    local_state.preamble_found = true;
    local_state.preamble_index = preamble_idx;
    local_state.coarse_freq_offset = freq_offset_coarse;
    printf("[DEBUG] Preamble: Found at AGC index %d (%.3f sec)\n",
           preamble_idx, (float)preamble_idx / samp_rate);

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

    // FIXED: Rewind burst start to capture signal before preamble detection peak
    // Preamble detection finds correlation peak (middle of preamble), not start
    // Rewind by 50ms to capture actual signal start (reduced from 200ms after extending search window)
    int rewind_samples = (int)(0.05f * samp_rate);  // 50ms = 125k samples @ 2.5MHz
    int burst_start_idx = (preamble_idx > rewind_samples) ? (preamble_idx - rewind_samples) : 0;

    // Adjust burst length to include rewinded samples
    burst_length += (preamble_idx - burst_start_idx);
    if (burst_start_idx + burst_length > num_samples) {
        burst_length = num_samples - burst_start_idx;
    }

    printf("  Burst start (rewinded): %d samples before preamble peak\n", preamble_idx - burst_start_idx);
    printf("  Burst length: %zu samples (%.2f sec)\n",
       burst_length, (float)burst_length / samp_rate);
    printf("[DEBUG] Burst extraction: AGC[%d .. %zu] → burst[0 .. %zu]\n",
       burst_start_idx, burst_start_idx + burst_length - 1, burst_length - 1);

    float complex *burst = &agc_out[burst_start_idx];

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
     printf("=== SIGNAL BEFORE TIMING RECOVERY ===\n");
        printf("First 5 QPSK samples magnitude:\n");
        for (int i = 0; i < 5; i++) {
            printf("  qpsk[%d]: mag=%.3f\n", i, cabsf(qpsk_out[i]));
        }
        
    int sps_main = (int)(samp_rate / DSSS_CHIP_RATE + 0.5f);
    
    // DEBUG: Vérifier les premières données OQPSK
    printf("=== OQPSK INPUT DEBUG ===\n");
    for (int i = 0; i < 5; i++) {
        printf("  oqpsk[%d]: real=%.3f, imag=%.3f, mag=%.3f\n", 
            i, crealf(fine_sync_out[i]), cimagf(fine_sync_out[i]), 
            cabsf(fine_sync_out[i]));
    }
    for (int i = 32; i < 37; i++) {
        printf("  oqpsk[%d]: real=%.3f, imag=%.3f, mag=%.3f\n", 
            i, crealf(fine_sync_out[i]), cimagf(fine_sync_out[i]), 
            cabsf(fine_sync_out[i]));
    }
    oqpsk_to_qpsk(fine_sync_out, qpsk_out, burst_length, sps_main);
    printf("  Applied Tc/2 delay: %d samples\n", sps_main / 2);

    // Update burst_length to account for delay
    size_t qpsk_length = burst_length - sps_main / 2;
    
    /**
 * @brief Sauvegarder la constellation pour analyse
 */
    /*void dump_constellation(const float complex *symbols, size_t num_symbols, const char *filename) {
        FILE *f = fopen(filename, "w");
        if (!f) return;
        
        fprintf(f, "real,imag\n");
        for (size_t i = 0; i < num_symbols && i < 1000; i++) {
            fprintf(f, "%.6f,%.6f\n", crealf(symbols[i]), cimagf(symbols[i]));
        }
        fclose(f);
        printf("[DEBUG] Constellation saved to %s\n", filename);
    }*/

    // Step 5: Timing recovery
    printf("Step 5: Timing recovery...\n");
    float complex *symbols = malloc(40000 * sizeof(float complex));
    size_t num_symbols = 0;
    if (!symbols) {
        free(agc_out);
        free(freq_corr_out);
        free(fine_sync_out);
        free(qpsk_out);
        return -1;
    }   
    
    // CORRECTION : Commencer au 2ème échantillon (index 1) car qpsk[0] = 0.000
    dsss_timing_recovery_corrected(&qpsk_out[1], symbols, qpsk_length - 1, &num_symbols, samp_rate);
    printf("  Recovered %zu symbols\n", num_symbols);
    
    // DEBUG: Analyser la distribution des symboles
    int quadrant_count[4] = {0};
    for (int i = 0; i < 1000 && i < num_symbols; i++) {
        float real = crealf(symbols[i]);
        float imag = cimagf(symbols[i]);
    
        if (real >= 0 && imag >= 0) quadrant_count[0]++;
        else if (real >= 0 && imag < 0) quadrant_count[1]++;
        else if (real < 0 && imag >= 0) quadrant_count[2]++;
        else quadrant_count[3]++;
    }

    printf("=== SYMBOL DISTRIBUTION ANALYSIS ===\n");
    printf("Quadrant I (++): %d symbols\n", quadrant_count[0]);
    printf("Quadrant II (+-): %d symbols\n", quadrant_count[1]); 
    printf("Quadrant III (-+): %d symbols\n", quadrant_count[2]);
    printf("Quadrant IV (--): %d symbols\n", quadrant_count[3]);
    printf("Total analyzed: %d symbols\n", quadrant_count[0] + quadrant_count[1] + quadrant_count[2] + quadrant_count[3]);
    
    printf("=== SYMBOLS RIGHT AFTER TIMING RECOVERY ===\n");
    for (int i = 0; i < 3 && i < num_symbols; i++) {
        printf("  symbols[%d]: real=%.3f, imag=%.3f, mag=%.3f\n", 
            i, crealf(symbols[i]), cimagf(symbols[i]), cabsf(symbols[i]));
    }


    // Debug du signal QPSK
    printf("=== QPSK SIGNAL CHECK ===\n");
    printf("qpsk[0]=%.3f+j%.3f (should be non-zero)\n", crealf(qpsk_out[0]), cimagf(qpsk_out[0]));
    printf("qpsk[1]=%.3f+j%.3f\n", crealf(qpsk_out[1]), cimagf(qpsk_out[1]));
    printf("qpsk[2]=%.3f+j%.3f\n", crealf(qpsk_out[2]), cimagf(qpsk_out[2]));
    
    // Step 5.5: Apply lowpass filter to improve SNR
    //printf("Step 5.5: Lowpass filtering (cutoff=3kHz)...\n");
    //apply_lowpass_filter(symbols, num_symbols, samp_rate);
    //printf("  Filtering complete\n");
    
    printf("=== SYMBOLS AFTER LOWPASS FILTER ===\n");
    for (int i = 0; i < 3 && i < num_symbols; i++) {
        printf("  symbols[%d]: real=%.3f, imag=%.3f, mag=%.3f\n", 
            i, crealf(symbols[i]), cimagf(symbols[i]), cabsf(symbols[i]));
    }

    // Step 6: Phase ambiguity resolution
    printf("=== SYMBOL VALUES CHECK ===\n");
    printf("First 5 symbols after phase correction:\n");
    for (int i = 0; i < 5; i++) {
        printf("  symbol[%d]: real=%.3f, imag=%.3f, mag=%.3f\n", 
           i, crealf(symbols[i]), cimagf(symbols[i]), cabsf(symbols[i]));
    }
    printf("Step 6: Phase ambiguity resolution...\n");
    int phase_rot;
    bool iq_swap;

    if (dsss_resolve_phase_ambiguity(symbols, num_symbols,
                                    &phase_rot, &iq_swap, &local_state) < 0) {
        fprintf(stderr, "Warning: Phase ambiguity resolution uncertain\n");
    }

    local_state.phase_rotation = phase_rot;
    //local_state.iq_swapped = iq_swap;
    
/*    printf("[DEBUG] APPLYING PHASE ROTATION: %.1f°\n", local_state.phase_angle_deg);
    float angle_rad = local_state.phase_angle_deg * M_PI / 180.0f;
    float complex rotation = cexpf(I * angle_rad);

    for (size_t i = 0; i < num_symbols; i++) {
        symbols[i] = symbols[i] * rotation;
    }*/

    printf("After phase rotation - First 3 symbols:\n");
    for (int i = 0; i < 3; i++) {
        printf("  symbol[%d]: %.3f+j%.3f\n", i, crealf(symbols[i]), cimagf(symbols[i]));
    }
    
    printf("=== SYMBOLS AFTER PHASE CORRECTION ===\n");
    for (int i = 0; i < 3 && i < num_symbols; i++) {
        printf("  symbols[%d]: real=%.3f, imag=%.3f, mag=%.3f\n", 
            i, crealf(symbols[i]), cimagf(symbols[i]), cabsf(symbols[i]));
    }
    // FORCE les paramètres corrects pour ton signal de test
printf("=== FORCAGE MANUEL DES PARAMETRES ===\n");
local_state.phase_angle_deg = 0.0f;      // Phase à 0°
local_state.iq_swapped = false;          // Pas de swap I/Q
local_state.bit_invert = true;           // ESSAYE ÇA - inversion souvent nécessaire
local_state.chip_convention = 0;         // Real>0 → 0
local_state.prn_conversion = 0;          // -1 → 1, +1 → 0 (convention T.018)
local_state.interleaving = 0;            // I,Q,I,Q
local_state.chip_offset = 0;             // Pas de décalage

// Réappliquer les corrections
float complex rot = cexpf(I * 0.0f);
for (size_t i = 0; i < num_symbols; i++) {
    symbols[i] = symbols[i] * rot;
}

/**
 * @brief Test d'alignement chips/PRN pour trouver le bon offset
 * @return Le meilleur offset trouvé
 */
int test_chip_alignment(const uint8_t *chips_i, const uint8_t *chips_q, size_t num_chips) {
    printf("=== CHIP ALIGNMENT TEST ===\n");
    
    prn_state_t prn_state;
    prn_init(&prn_state, 0);
    
    float best_correlation = 0.0f;
    int best_offset = 0;
    
    // Tester différents offsets
    for (int offset = -20; offset <= 20; offset++) {
        int matches = 0;
        int total = 0;
        
        // Comparer les premiers 1000 chips avec PRN attendu
        for (int i = 0; i < 1000; i++) {
            int chip_idx = i + offset;
            if (chip_idx >= 0 && chip_idx < num_chips) {
                // Générer PRN attendu
                int8_t prn_i[256], prn_q[256];
                if (i % 256 == 0) {
                    prn_generate_i(&prn_state, prn_i);
                    prn_generate_q(&prn_state, prn_q);
                }
                
                uint8_t expected_i = (prn_i[i % 256] == -1) ? 1 : 0;
                uint8_t expected_q = (prn_q[i % 256] == -1) ? 1 : 0;
                
                if (chips_i[chip_idx] == expected_i) matches++;
                if (chips_q[chip_idx] == expected_q) matches++;
                total += 2;
            }
        }
        
        float correlation = (float)matches / total;
        printf("  Offset %+3d: %.3f correlation", offset, correlation);
        
        if (correlation > best_correlation) {
            best_correlation = correlation;
            best_offset = offset;
        }
        printf("\n");
    }
    
    printf("=== BEST OFFSET: %+d (correlation: %.3f) ===\n", best_offset, best_correlation);
    return best_offset;
}

    // NOTE: Phase rotation and I/Q swap already applied by dsss_resolve_phase_ambiguity()
    // No need to re-apply here

// Step 7: QPSK demodulation with automatic convention detection...
printf("Step 7: QPSK demodulation with automatic convention detection...\n");
uint8_t *chips_i = calloc(num_symbols, sizeof(uint8_t));
uint8_t *chips_q = calloc(num_symbols, sizeof(uint8_t));

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

// =============================================================================
// DÉTECTION AUTOMATIQUE DE LA CONVENTION DE CONVERSION
// =============================================================================

bool bit_invert = local_state.bit_invert;

// Tester les 4 combinaisons de conventions possibles
float best_corr = 0.0f;
int best_conv_i = 0, best_conv_q = 0;

printf("  Testing chip conversion conventions:\n");
for (int conv_i = 0; conv_i < 2; conv_i++) {
    for (int conv_q = 0; conv_q < 2; conv_q++) {
        float corr = test_chip_convention(symbols, num_symbols, conv_i, conv_q, bit_invert);
        printf("    Convention I=%d, Q=%d: corr=%.3f", conv_i, conv_q, corr);
        
        if (corr > best_corr) {
            best_corr = corr;
            best_conv_i = conv_i;
            best_conv_q = conv_q;
            printf(" ← NEW BEST");
        }
        printf("\n");
    }
}

printf("  SELECTED: Convention I=%d (real>0→%d), Q=%d (imag>0→%d) with corr=%.3f\n", 
       best_conv_i, best_conv_i, best_conv_q, best_conv_q, best_corr);

// Stocker les conventions détectées dans l'état
local_state.auto_conv_i = best_conv_i;
local_state.auto_conv_q = best_conv_q;

// =============================================================================
// APPLIQUER LA MEILLEURE CONVENTION À TOUS LES SYMBOLES
// =============================================================================

for (size_t i = 0; i < num_symbols; i++) {
    // Appliquer la convention détectée
    chips_i[i] = (crealf(symbols[i]) >= 0) ? best_conv_i : (1 - best_conv_i);
    chips_q[i] = (cimagf(symbols[i]) >= 0) ? best_conv_q : (1 - best_conv_q);
    
    // Appliquer l'inversion si nécessaire (trouvée par phase resolution)
    if (bit_invert) {
        chips_i[i] = 1 - chips_i[i];
        chips_q[i] = 1 - chips_q[i];
    }
    
    // Debug des premières conversions
    if (i < 10) {
        printf("[AUTO CONVERSION] symbol[%zu]: real=%.3f → chip_i=%d, imag=%.3f → chip_q=%d\n",
               i, crealf(symbols[i]), chips_i[i], cimagf(symbols[i]), chips_q[i]);
    }
}
    
// ==========================
// APPLY I/Q SWAP IF NEEDED 
//===========================
if (local_state.iq_swapped) {
    printf("[DEBUG] APPLYING I/Q SWAP\n");
    for (size_t i = 0; i < num_symbols; i++) {
        uint8_t temp = chips_i[i];
        chips_i[i] = chips_q[i];
        chips_q[i] = temp;
    }
    
    printf("After swap - First 5 chips:\n");
    for (int i = 0; i < 5; i++) {
        printf("  [%d] I=%d, Q=%d\n", i, chips_i[i], chips_q[i]);
    }
}

    test_chip_alignment(chips_i, chips_q, num_symbols);
    
    // Afficher les chips APRÈS conversion
    printf("=== FIRST CHIPS vs PRN COMPARISON ===\n");

    // Régénérer le PRN pour l'affichage
    prn_state_t prn_display;
    prn_init(&prn_display, 0);
    int8_t prn_i_display[256], prn_q_display[256];
    prn_generate_i(&prn_display, prn_i_display);
    prn_generate_q(&prn_display, prn_q_display);

    printf("First 10 chips I: ");
    for (int i = 0; i < 10; i++) printf("%d", chips_i[i]);
        printf("\nFirst 10 PRN I:   ");
    for (int i = 0; i < 10; i++) printf("%d", (prn_i_display[i] == -1) ? 1 : 0);
        printf("\n");

    printf("First 10 chips Q: ");
    for (int i = 0; i < 10; i++) printf("%d", chips_q[i]);
        printf("\nFirst 10 PRN Q:   ");  
    for (int i = 0; i < 10; i++) printf("%d", (prn_q_display[i] == -1) ? 1 : 0);
        printf("\n");
    
    printf("[DEBUG] Symbol to chips: symbols[0 .. %zu] → chips_i/q[0 .. %zu] (1:1 mapping)\n",
           num_symbols - 1, num_symbols - 1);
    printf("Chip value validation - I[0]=%d, Q[0]=%d\n", chips_i[0], chips_q[0]);
    if (chips_i[0] > 1 || chips_q[0] > 1) {
        printf("❌ ERROR: Chip values not binary! I[0]=%d, Q[0]=%d\n", chips_i[0], chips_q[0]);
    }
    printf("=== CONVERSION CHECK ===\n");
    printf("Symbol[0]: real=%.3f → chip_i=%d\n", crealf(symbols[0]), chips_i[0]);
    printf("Symbol[0]: imag=%.3f → chip_q=%d\n", cimagf(symbols[0]), chips_q[0]);
    printf("bit_invert=%d\n", bit_invert);
    
    
    // DEBUG: Vérifier les premiers chips PRN générés
printf("=== PRN GENERATION VERIFICATION ===\n");
prn_state_t prn_debug;
prn_init(&prn_debug, 0);

int8_t debug_prn_i[256], debug_prn_q[256];  // NOMS DIFFÉRENTS
prn_generate_i(&prn_debug, debug_prn_i);
prn_generate_q(&prn_debug, debug_prn_q);

printf("First 10 PRN I chips (signed): ");
for (int i = 0; i < 10; i++) {
    printf("%+d ", debug_prn_i[i]);
}
printf("\n");

printf("First 10 PRN Q chips (signed): ");
for (int i = 0; i < 10; i++) {
    printf("%+d ", debug_prn_q[i]);
}
printf("\n");

// Convert to binary selon la convention du diagnostic
printf("First 10 PRN I chips (binary, -1→1, +1→0): ");
for (int i = 0; i < 10; i++) {
    printf("%d", (debug_prn_i[i] == -1) ? 1 : 0);
}
printf(" (should match: 1000000000)\n");

printf("First 10 PRN Q chips (binary, -1→1, +1→0): ");
for (int i = 0; i < 10; i++) {
    printf("%d", (debug_prn_q[i] == -1) ? 1 : 0);
}
printf(" (should match: 1000001000)\n");

// TESTER l'autre convention aussi
printf("First 10 PRN I chips (binary, -1→0, +1→1): ");
for (int i = 0; i < 10; i++) {
    printf("%d", (debug_prn_i[i] == -1) ? 0 : 1);
}
printf("  ← ESSAYER CETTE CONVENTION\n");

printf("First 10 PRN Q chips (binary, -1→0, +1→1): ");
for (int i = 0; i < 10; i++) {
    printf("%d", (debug_prn_q[i] == -1) ? 0 : 1);
}
printf("  ← ESSAYER CETTE CONVENTION\n");

    // =========================================================================
    // DIAGNOSTIC TOOL: Test all despreading parameter combinations
    // =========================================================================
    // Run diagnostic on preamble (50 bits = 12,800 chips) to find optimal params
    const size_t preamble_chips = DSSS_PREAMBLE_LENGTH * DSSS_SPREADING_FACTOR;
    if (num_symbols >= preamble_chips) {
        int best_chip_conv, best_prn_conv, best_interleave, best_offset;
        float diag_corr = dsss_diagnose_despreading(chips_i, chips_q, preamble_chips,
                                                     &best_chip_conv, &best_prn_conv,
                                                     &best_interleave, &best_offset);
    
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
                         dsss_demod_state_t *state, float manual_sr, float forced_freq) {
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

    // Determine sample rate (manual or auto-detect)
    printf("\n");
    float sample_rate;
    if (manual_sr > 0) {
        printf("Using manual sample rate: %.0f Hz (sps=%.1f)\n",
               manual_sr, manual_sr / DSSS_CHIP_RATE);
        sample_rate = manual_sr;
    } else {
        printf("Auto-detecting sample rate...\n");
        sample_rate = dsss_estimate_sample_rate(iq_samples, num_samples);
    }

    // Demodulate with sample rate and forced frequency offset
    int result = dsss_demodulate(iq_samples, num_samples, output_bits,
                                 sample_rate, state, forced_freq);

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
    printf("Auto Convention: I=%d (real>0→%d), Q=%d (imag>0→%d), corr=%.3f\n",
           state->auto_conv_i, state->auto_conv_i,
           state->auto_conv_q, state->auto_conv_q,
           state->auto_conv_correlation);
    printf("Quality: SNR=%.1f dB, AGC gain=%.2f\n",
           state->snr_estimate, state->agc_gain);
    printf("DSSS: mean_corr=%.3f, bits_decoded=%d\n",
           state->mean_correlation, state->bits_decoded);
    printf("==============================\n\n");
}
