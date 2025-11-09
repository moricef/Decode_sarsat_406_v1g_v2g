/**
 * @file dsss_demod.h
 * @brief DSSS OQPSK Receiver API - MATLAB Coder Implementation
 *
 * This header provides the public API for COSPAS-SARSAT DSSS OQPSK demodulation.
 * The implementation uses MATLAB Coder generated code for 100% MATLAB compatibility.
 *
 * Previous manual C implementation archived in: archive/dsss_demod.c
 */

#ifndef DSSS_DEMOD_H
#define DSSS_DEMOD_H

#include <stdint.h>
#include <stddef.h>
#include <complex.h>

// DSSS Signal Parameters (COSPAS-SARSAT T.018)
#define DSSS_CHIP_RATE          38400   // 38.4 kchips/s
#define DSSS_SPREADING_FACTOR   256     // 256 chips per symbol

// Frame Structure
#define DSSS_PREAMBLE_BITS      50      // 50-bit preamble (all zeros)
#define DSSS_PAYLOAD_BITS       202     // 202 information bits
#define DSSS_PARITY_BITS        48      // 48 BCH parity bits
#define DSSS_PACKET_BITS        300     // Total: 50 + 202 + 48 = 300 bits

// Total frame length in chips (for OQPSK, 2 bits per symbol)
#define DSSS_PACKET_CHIPS       (DSSS_PACKET_BITS * DSSS_SPREADING_FACTOR / 2)  // 38400 chips

/**
 * @brief Receive and demodulate DSSS OQPSK burst
 *
 * This function performs complete DSSS OQPSK demodulation using MATLAB Coder generated code.
 * The implementation is 100% identical to the MATLAB reference implementation.
 *
 * Processing steps:
 * 1. AGC normalization (updates every 10*sps samples)
 * 2. Polyphase correlator for preamble detection
 * 3. Carrier synchronization (processes ALL samples)
 * 4. Timing recovery
 * 5. OQPSK demodulation
 * 6. DSSS despreading
 * 7. BCH error correction
 *
 * @param ota_buffer    Input IQ samples (complex baseband, float complex)
 * @param buffer_length Number of input samples (must be >= 307200 for sps=8)
 * @param sps           Samples per symbol (must be 8)
 * @param fs            Sampling frequency in Hz (307200 Hz nominal)
 * @param max_doppler   Maximum Doppler shift to search in Hz (unused in MATLAB version)
 * @param output_bits   Output buffer for decoded bits (202 information bits + 48 zeros)
 *
 * @return 0 on success, -1 on error
 *
 * @note The MATLAB implementation requires exactly:
 *       - sps = 8 (samples per symbol)
 *       - 307200 samples minimum (38.4 kHz * 8 sps = 307.2 kHz sampling)
 *       - No Doppler compensation (assumes signal is pre-centered)
 *
 * @note Output contains 202 information bits (BCH payload) followed by 48 zeros.
 *       The 48 BCH parity bits are computed internally but not returned.
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits);

/**
 * @brief Generate PRN sequence (DEPRECATED)
 *
 * This function is deprecated. PRN generation is handled internally
 * by the MATLAB Coder implementation.
 *
 * @deprecated Use MATLAB Coder internal PRN generation
 */
void dsss_generate_prn(uint32_t initial_state, int8_t *output, size_t num_chips);

/**
 * @brief Generate PRN I/Q sequences (DEPRECATED)
 *
 * This function is deprecated. PRN generation is handled internally
 * by the MATLAB Coder implementation.
 *
 * @deprecated Use MATLAB Coder internal PRN generation
 */
void dsss_generate_prn_sequences(int8_t *prn_i, int8_t *prn_q);

#endif /* DSSS_DEMOD_H */
