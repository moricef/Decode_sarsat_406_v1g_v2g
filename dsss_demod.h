/**
 * @file dsss_demod.h
 * @brief Complete DSSS OQPSK Receiver for COSPAS-SARSAT 406 MHz
 */

#ifndef DSSS_DEMOD_H
#define DSSS_DEMOD_H

#include <stdint.h>
#include <stddef.h>
#include <complex.h>

#define DSSS_CHIP_RATE          38400
#define DSSS_SPREADING_FACTOR   256

#define DSSS_PREAMBLE_BITS      50
#define DSSS_PAYLOAD_BITS       202
#define DSSS_PARITY_BITS        48
#define DSSS_PACKET_BITS        300

#define DSSS_PACKET_CHIPS       (DSSS_PACKET_BITS * DSSS_SPREADING_FACTOR / 2)

void dsss_generate_prn(uint32_t initial_state, int8_t *output, size_t num_chips);
void dsss_generate_prn_sequences(int8_t *prn_i, int8_t *prn_q);

/**
 * @brief Receive and demodulate DSSS OQPSK burst
 * @param ota_buffer Input samples (complex baseband)
 * @param buffer_length Number of input samples
 * @param sps Samples per symbol (oversampling factor)
 * @param fs Sampling frequency in Hz
 * @param max_doppler Maximum Doppler shift to search (Hz)
 * @param output_bits Output buffer for 250 decoded bits
 * @return 0 on success, -1 on error
 *
 * This function performs complete DSSS OQPSK demodulation:
 * - AGC normalization
 * - Preamble detection with frequency search
 * - Coarse and fine frequency correction
 * - Timing recovery
 * - Phase ambiguity resolution
 * - DSSS despreading
 *
 * Output is 250 raw bits that should be passed to BCH decoder.
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits);

#endif
