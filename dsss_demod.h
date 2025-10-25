/**
 * @file dsss_demod.h
 * @brief DSSS/OQPSK Demodulator for COSPAS-SARSAT 2G Beacons (T.018)
 *
 * ⚠️  WARNING: THIS IMPLEMENTATION IS NOT FUNCTIONAL ⚠️
 *
 * Current Status: PAUSED - 55.3% bit accuracy (need >95%)
 * Date: 2025-10-19
 *
 * See dsss_demod.c header and ETAT_PAUSE_DEMODULATEUR.md for:
 * - Complete bug analysis
 * - Test results
 * - Options for future resumption
 *
 * Implements complete receiver chain:
 * - AGC (Automatic Gain Control) ✅
 * - Preamble detection with frequency search ✅
 * - Frequency offset correction (coarse + fine) ⚠️ Fine disabled
 * - Timing recovery (symbol synchronization) ❌ BUGGY
 * - Phase ambiguity resolution ❌ BUGGY (wrong order)
 * - DSSS despreading (PRN correlation) ❌ BUGGY (low correlation)
 * - OQPSK demodulation (odd/even bit separation) ✅
 *
 * Based on:
 * - MATLAB DSSSReceiverForSARbasedTrackingSystem.pdf (MathWorks R2024a)
 * - C/S T.018 Rev.12 Specification
 *
 * @author Collaborative development (2025)
 * @license Creative Commons CC BY-NC-SA
 */

#ifndef DSSS_DEMOD_H
#define DSSS_DEMOD_H

#include <stdint.h>
#include <stddef.h>
#include <complex.h>
#include <stdbool.h>

// =============================================================================
// SYSTEM PARAMETERS (T.018 Specification)
// =============================================================================

#define DSSS_CHIP_RATE          38400       // 38.4 kchips/s
#define DSSS_DATA_RATE          300         // 300 bps
#define DSSS_SPREADING_FACTOR   256         // 256 chips per bit
#define DSSS_SAMPLE_RATE        400000      // 400 kHz (SARSAT_SGB default)
#define DSSS_SAMPLES_PER_CHIP   10.416667f  // 400k / 38.4k

#define DSSS_PREAMBLE_LENGTH    50          // 50 bits preamble
#define DSSS_PAYLOAD_LENGTH     250         // 250 bits payload (202 data + 48 BCH)
#define DSSS_TOTAL_BITS         300         // Total frame length

#define DSSS_MAX_DOPPLER        750         // ±750 Hz (phase dev, PlutoSDR+osc.ext, tests sol)
#define DSSS_FREQ_SEARCH_STEP   150         // 150 Hz frequency search step

// =============================================================================
// DEMODULATOR STATE
// =============================================================================

/**
 * @brief Demodulator state and statistics
 */
typedef struct {
    // Detection
    bool preamble_found;
    int preamble_index;

    // Frequency
    float coarse_freq_offset;       // Hz
    float fine_freq_offset;         // Hz
    float total_freq_offset;        // Hz

    // Timing
    bool timing_locked;
    float timing_error_std;

    // Phase
    int phase_rotation;             // 0, 1, 2, 3 (0°, 90°, 180°, 270°) - legacy
    float phase_angle_deg;          // Continuous phase angle (0-360°)
    bool iq_swapped;
    bool bit_invert;                // Bit inversion flag (0/1 convention)

    // Quality
    float snr_estimate;             // dB
    float agc_gain;                 // Linear gain
    float correlation_peak;         // Preamble correlation peak

    // DSSS
    float mean_correlation;         // Mean despreading correlation
    int bits_decoded;               // Number of bits successfully decoded

    // Despreading parameters (Option C - iterative search)
    int chip_convention;            // 0: Real>0→0, 1: Real>0→1
    int prn_conversion;             // 0: -1→1, 1: -1→0
    int interleaving;               // 0: I,Q,I,Q, 1: Q,I,Q,I
    int chip_offset;                // Optimal chip offset (-10 to +10)

} dsss_demod_state_t;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Demodulate OQPSK DSSS IQ samples to bits
 *
 * Complete receiver chain:
 * 1. AGC
 * 2. Preamble detection (with freq search)
 * 3. Frequency correction (coarse + fine)
 * 4. Timing recovery
 * 5. Phase ambiguity resolution
 * 6. DSSS despreading
 * 7. OQPSK demodulation
 *
 * @param iq_samples    Input IQ samples (complex float)
 * @param num_samples   Number of samples
 * @param output_bits   Output 300-bit array (preallocated)
 * @param samp_rate     Sample rate in Hz (default: 2.5 MHz)
 * @param state         Output state structure (can be NULL)
 * @param forced_freq   Forced frequency offset in Hz (NAN = auto-search)
 * @return 0 on success, -1 on error, -2 if preamble not found
 */
int dsss_demodulate(const float complex *iq_samples,
                    size_t num_samples,
                    uint8_t *output_bits,
                    float samp_rate,
                    dsss_demod_state_t *state,
                    float forced_freq);

/**
 * @brief Load IQ file and demodulate
 *
 * Supports formats:
 * - .iq: Raw complex float32 (interleaved I/Q)
 * - .cfile: GNU Radio complex float format
 *
 * @param filename      IQ file path
 * @param output_bits   Output 300-bit array (preallocated)
 * @param state         Output state structure (can be NULL)
 * @param manual_sr     Manual sample rate (0 = auto-detect)
 * @param forced_freq   Forced frequency offset in Hz (NAN = auto-search)
 * @return 0 on success, -1 on error, -2 if preamble not found
 */
int dsss_demodulate_file(const char *filename,
                         uint8_t *output_bits,
                         dsss_demod_state_t *state,
                         float manual_sr,
                         float forced_freq);

/**
 * @brief Print demodulator state and statistics
 *
 * @param state Demodulator state
 */
void dsss_print_state(const dsss_demod_state_t *state);

// =============================================================================
// INTERNAL FUNCTIONS (exposed for testing)
// =============================================================================

/**
 * @brief Apply AGC to normalize signal power
 *
 * @param input         Input samples
 * @param output        Output normalized samples
 * @param num_samples   Number of samples
 * @param gain_out      Output: AGC gain applied
 * @return 0 on success
 */
int dsss_agc(const float complex *input,
             float complex *output,
             size_t num_samples,
             float *gain_out);

/**
 * @brief Estimate sample rate by testing multiple candidates
 *
 * Tests common SDR sample rates and chooses the one that maximizes
 * preamble correlation
 *
 * @param samples       Input samples
 * @param num_samples   Number of samples
 * @return Estimated sample rate (Hz)
 */
float dsss_estimate_sample_rate(const float complex *samples,
                                size_t num_samples);

/**
 * @brief Detect preamble with frequency offset search
 *
 * Searches for 50-bit preamble across frequency offsets
 *
 * @param samples       Input samples (after AGC)
 * @param num_samples   Number of samples
 * @param samp_rate     Sample rate
 * @param preamble_idx  Output: preamble start index
 * @param freq_offset   Output: estimated frequency offset (Hz)
 * @param correlation   Output: correlation peak value
 * @return 0 if found, -1 if not found
 */
int dsss_detect_preamble(const float complex *samples,
                         size_t num_samples,
                         float samp_rate,
                         int *preamble_idx,
                         float *freq_offset,
                         float *correlation);

/**
 * @brief Apply frequency offset correction
 *
 * @param input         Input samples
 * @param output        Output corrected samples
 * @param num_samples   Number of samples
 * @param freq_offset   Frequency offset to correct (Hz)
 * @param samp_rate     Sample rate
 */
void dsss_frequency_correction(const float complex *input,
                               float complex *output,
                               size_t num_samples,
                               float freq_offset,
                               float samp_rate);

/**
 * @brief Fine frequency and phase tracking (Costas loop)
 *
 * @param input         Input samples
 * @param output        Output phase-corrected samples
 * @param num_samples   Number of samples
 * @param samp_rate     Sample rate
 * @param freq_est      Output: fine frequency estimate (Hz)
 * @return 0 on success
 */
int dsss_fine_frequency_sync(const float complex *input,
                             float complex *output,
                             size_t num_samples,
                             float samp_rate,
                             float *freq_est);

/**
 * @brief Symbol timing recovery
 *
 * Uses Gardner timing error detector
 *
 * @param input         Input samples (oversampled)
 * @param output        Output symbol-rate samples
 * @param num_samples   Input sample count
 * @param num_symbols   Output: number of symbols recovered
 * @param samp_rate     Sample rate
 * @return 0 on success
 */
int dsss_timing_recovery(const float complex *input,
                         float complex *output,
                         size_t num_samples,
                         size_t *num_symbols,
                         float samp_rate);

/**
 * @brief Resolve phase ambiguity using preamble
 *
 * Tests 4 rotations × 2 I/Q swaps = 8 combinations
 *
 * @param symbols       Input QPSK symbols
 * @param num_symbols   Number of symbols
 * @param phase_rot     Output: phase rotation (0-3)
 * @param iq_swap       Output: I/Q swap flag
 * @return 0 on success, -1 if preamble not found
 */
int dsss_resolve_phase_ambiguity(float complex *symbols,
                                 size_t num_symbols,
                                 int *phase_rot,
                                 bool *iq_swap,
                                 dsss_demod_state_t *state);

/**
 * @brief Despread DSSS signal using PRN correlation
 *
 * Core DSSS despreading:
 * 1. Generate PRN sequences (I and Q)
 * 2. Correlate received chips with PRN (XOR)
 * 3. Majority vote decision (>128/256)
 * 4. Interleave I/Q bits (odd→I, even→Q)
 *
 * @param chips_i       Input I-channel chips (38400 chips)
 * @param chips_q       Input Q-channel chips (38400 chips)
 * @param output_bits   Output 300 bits
 * @param correlation   Output: mean correlation metric
 * @return 0 on success
 */
int dsss_despread(const uint8_t *chips_i,
                  const uint8_t *chips_q,
                  uint8_t *output_bits,
                  float *correlation,
                  const dsss_demod_state_t *state);

#endif // DSSS_DEMOD_H
