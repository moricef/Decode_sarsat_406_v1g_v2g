/**
 * @file dsss_demod.h
 * @brief DSSS/OQPSK Demodulator for COSPAS-SARSAT 2G Beacons (T.018)
 *
 * MATLAB-Compliant Implementation
 * Reference: DSSSReceiverForSARbasedTrackingSystem.pdf (MathWorks R2024a)
 *
 * Architecture: Standard PLL with 4 components
 * 1. Timing Error Detector (TED) - Zero-Crossing (decision-directed)
 * 2. Interpolator - Farrow piecewise parabolic (α=0.5)
 * 3. Interpolation Controller - Modulo-1 counter
 * 4. Loop Filter - Proportional-Integrator (PI)
 *
 * TED Choice Justification:
 * - Zero-Crossing optimal for SNR=24dB (MATLAB page 17)
 * - Decision-directed: uses reliable hard decisions after carrier sync
 * - Low self-noise: better precision than Gardner in high-SNR
 * - Bandwidth-independent: works with all excess bandwidth values
 *
 * @date 2025-01-11
 * @version 11.0 (complete rewrite from V10.2)
 */

#ifndef DSSS_DEMOD_H
#define DSSS_DEMOD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <complex.h>

// =============================================================================
// COSPAS-SARSAT T.018 SYSTEM PARAMETERS
// =============================================================================

#define DSSS_CHIP_RATE         38400    ///< Chip rate: 38.4 kchips/s
#define DSSS_SPREADING_FACTOR  256      ///< Chips per bit per channel
#define DSSS_PREAMBLE_LENGTH   50       ///< Preamble: 50 bits (all zeros)
#define DSSS_PAYLOAD_LENGTH    202      ///< Information message: 202 bits
#define DSSS_PARITY_LENGTH     48       ///< BCH parity: 48 bits
#define DSSS_TOTAL_BITS        300      ///< Total frame: 300 bits
#define DSSS_BITS_PER_CHANNEL  150      ///< 150 bits per I/Q channel
#define DSSS_PACKET_CHIPS      38400    ///< 150 bits × 256 chips/bit = 38400 chips

// =============================================================================
// TIMING RECOVERY - PLL ARCHITECTURE
// =============================================================================

/**
 * @brief Timing Recovery PLL State
 *
 * Implements comm.SymbolSynchronizer from MATLAB with 4 PLL components:
 * - TED (Timing Error Detector): Zero-Crossing (decision-directed)
 * - Interpolator: Farrow piecewise parabolic (α=0.5)
 * - Interpolation Controller: Modulo-1 counter
 * - Loop Filter: Proportional-Integrator (PI)
 *
 * Zero-Crossing TED (MATLAB page 16):
 *   e(k) = x_mid((k-1/2)Ts)[â_I(k-1) - â_I(k)] +
 *          y_mid((k-1/2)Ts)[â_Q(k-1) - â_Q(k)]
 * where â_I, â_Q are hard decisions on I/Q components
 */
typedef struct {
    // === PLL PARAMETERS (MATLAB page 12) ===
    int samples_per_symbol;          ///< sps (e.g., 4 for 153.6kHz, 65 for 2.5MHz)
    float normalized_loop_bw;        ///< B_n*T_s = 0.001 (MATLAB default)
    float damping_factor;            ///< ζ = 2.0 (MATLAB default)
    float detector_gain;             ///< K_p = 4.0 (MATLAB default)

    // === LOOP FILTER STATE (PI Filter) ===
    float k1;                        ///< Proportional gain
    float k2;                        ///< Integral gain
    float integrator;                ///< W[n] - integrator state

    // === INTERPOLATION CONTROLLER (Modulo-1 Counter) ===
    float mu;                        ///< Fractional interval ∈ [0,1)
    float strobe;                    ///< Sample counter (accumulates to sps)

    // === TIMING ERROR DETECTOR STATE (Zero-Crossing) ===
    float complex prev_mid_sample;   ///< x((k-1/2)T_s) - previous mid-point sample
    float prev_i_decision;           ///< â_I(k-1) - previous I hard decision
    float prev_q_decision;           ///< â_Q(k-1) - previous Q hard decision
    int first_symbol;                ///< Flag: 1 = first symbol (skip TED)

    // === INTERPOLATOR BUFFER (Farrow needs 4 samples) ===
    float complex buffer[8];         ///< Circular buffer for interpolation
    int buf_write_idx;               ///< Write index for circular buffer

    // === OQPSK STATE BUFFER (for Q-channel alignment) ===
    float complex *q_delay_buffer;   ///< Q-channel delay buffer (sps/2 samples)
    int q_delay_size;                ///< Size of Q delay buffer
    int q_delay_idx;                 ///< Current index in Q delay buffer

} timing_recovery_state_t;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Main DSSS receiver function
 *
 * Processes IQ samples through complete DSSS receiver chain:
 * 1. AGC (Automatic Gain Control)
 * 2. Preamble Detection (with frequency search)
 * 3. Coarse Frequency Correction
 * 4. Fine Frequency Correction (Carrier Synchronizer)
 * 5. Timing Recovery (MATLAB-compliant PLL)
 * 6. Phase Ambiguity Resolution
 * 7. QPSK Demodulation
 * 8. DSSS Despreading
 *
 * @param ota_buffer    Input IQ samples (complex float32)
 * @param buffer_length Number of IQ samples
 * @param sps           Samples per chip (e.g., 65 for 2.5 MHz)
 * @param fs            Sampling frequency in Hz (e.g., 2.5e6)
 * @param max_doppler   Maximum Doppler shift in Hz (e.g., 12000 for LEO)
 * @param output_bits   Output buffer for 250 decoded bits (preamble excluded)
 *
 * @return 0 on success, negative error code on failure:
 *         -1: Memory allocation error
 *         -2: Preamble not detected
 *         -3: Timing recovery failed
 *         -4: Despreading failed
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits);

// =============================================================================
// TIMING RECOVERY API
// =============================================================================

/**
 * @brief Initialize timing recovery PLL
 *
 * Sets up PLL components with MATLAB-compliant parameters.
 * Calculates loop filter gains using formulas from MathWorks documentation:
 *
 *   θ = (B_n*T_s / N_sps) / (ζ + 1/(4ζ))
 *   K1 = (-4ζθ) / ((1 + 2ζθ + θ²) * K_p)
 *   K2 = (-4θ²) / ((1 + 2ζθ + θ²) * K_p)
 *
 * @param state                Output: initialized PLL state
 * @param sps                  Samples per symbol (chip)
 * @param normalized_loop_bw   B_n*T_s (default: 0.001)
 * @param damping_factor       ζ (default: 2.0)
 * @param detector_gain        K_p (default: 4.0 for Zero-Crossing)
 *
 * @return 0 on success, -1 on memory allocation error
 */
int timing_recovery_init(timing_recovery_state_t *state,
                        int sps,
                        float normalized_loop_bw,
                        float damping_factor,
                        float detector_gain);

/**
 * @brief Process samples through timing recovery PLL
 *
 * Extracts symbols at chip rate from oversampled OQPSK signal.
 * Automatically handles OQPSK Q-channel alignment (Tc/2 delay).
 *
 * Algorithm per sample:
 * 1. Store in interpolation buffer
 * 2. Update strobe counter
 * 3. If strobe >= sps:
 *    a. Interpolate symbol at fractional position μ
 *    b. Extract mid-symbol for Gardner TED
 *    c. Compute timing error
 *    d. Update loop filter (PI)
 *    e. Update μ from loop filter output
 *
 * @param state        PLL state (must be initialized)
 * @param input        Input samples (OQPSK, oversampled at sps)
 * @param input_len    Number of input samples
 * @param output       Output symbols (QPSK, at chip rate)
 * @param output_len   Output: number of symbols extracted
 *
 * @return 0 on success, -1 on error
 */
int timing_recovery_process(timing_recovery_state_t *state,
                           const float complex *input,
                           size_t input_len,
                           float complex *output,
                           size_t *output_len);

/**
 * @brief Free timing recovery resources
 *
 * @param state PLL state to free
 */
void timing_recovery_free(timing_recovery_state_t *state);

// =============================================================================
// PRN GENERATION (T.018 Compliant)
// =============================================================================

/**
 * @brief Generate I and Q PRN sequences
 *
 * Generates 38400 chips for each channel using:
 * - LFSR polynomial: x^23 + x^18 + 1
 * - RIGHT shift, LSB output (T.018 Appendix D)
 * - Initial states from T.018 Table 2.2:
 *   * Normal I: 0x000001 → first 64 chips: 8000 0108 4212 84A1
 *   * Normal Q: 0x1AC1FC → first 64 chips: 3F83 58BA D030 F231
 *
 * @param prn_i Output: I-channel PRN (38400 chips, values: -1 or +1)
 * @param prn_q Output: Q-channel PRN (38400 chips, values: -1 or +1)
 */
void dsss_generate_prn_sequences(int8_t *prn_i, int8_t *prn_q);

#endif // DSSS_DEMOD_H
