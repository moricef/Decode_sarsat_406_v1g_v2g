/**
 * @file dsss_demod_matlab.c
 * @brief Adapter between our interface and MATLAB Coder generated dsss_receiver
 *
 * This file provides a clean interface to the MATLAB Coder generated code,
 * wrapping it with our existing API for seamless integration.
 */

#include "dsss_demod.h"
#include "dsss_receiver.h"
#include "dsss_receiver_types.h"
#include "dsss_receiver_initialize.h"
#include "dsss_receiver_terminate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MATLAB expects 307200 samples exactly (38.4 sec at 8 sps)
#define MATLAB_EXPECTED_SAMPLES 307200

/**
 * @brief Receive and demodulate DSSS OQPSK burst using MATLAB Coder implementation
 *
 * This is a wrapper around the MATLAB Coder generated dsss_receiver function.
 * It adapts our API to match the MATLAB interface.
 *
 * @param ota_buffer Input samples (complex baseband, float complex)
 * @param buffer_length Number of input samples
 * @param sps Samples per symbol (must be 8)
 * @param fs Sampling frequency in Hz (should be 307200)
 * @param max_doppler Maximum Doppler shift to search (Hz) - unused in MATLAB version
 * @param output_bits Output buffer for 250 decoded bits (only 202 info bits will be filled)
 * @return 0 on success, -1 on error
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits) {

    (void)fs;  // Not used - MATLAB assumes 307200 Hz
    (void)max_doppler;  // Not used - MATLAB doesn't do Doppler search

    printf("\n=== MATLAB Coder DSSS Receiver ===\n");

    // Validate parameters
    if (sps != 8) {
        fprintf(stderr, "ERROR: MATLAB Coder requires sps=8, got sps=%d\n", sps);
        return -1;
    }

    if (buffer_length < MATLAB_EXPECTED_SAMPLES) {
        fprintf(stderr, "ERROR: MATLAB Coder requires at least %d samples, got %zu\n",
                MATLAB_EXPECTED_SAMPLES, buffer_length);
        return -1;
    }

    // Initialize MATLAB runtime (safe to call multiple times)
    dsss_receiver_initialize();

    // Allocate MATLAB-compatible buffer (creal_T = double complex)
    static creal_T otaBuffer[MATLAB_EXPECTED_SAMPLES];

    // Convert float complex to creal_T (double complex)
    printf("Converting %d samples from float to double precision...\n", MATLAB_EXPECTED_SAMPLES);
    for (int i = 0; i < MATLAB_EXPECTED_SAMPLES; i++) {
        otaBuffer[i].re = (double)crealf(ota_buffer[i]);
        otaBuffer[i].im = (double)cimagf(ota_buffer[i]);
    }

    // Configure MATLAB settings
    struct0_T settings;
    settings.velocity = 0.0;        // No Doppler compensation
    settings.txPower = 1.0;         // Normalized power
    settings.oversampling = 4.0;    // sps = 2 * oversampling = 8

    printf("Settings: velocity=%.1f, txPower=%.1f, oversampling=%.1f (sps=%d)\n",
           settings.velocity, settings.txPower, settings.oversampling, sps);

    // Output buffers
    boolean_T rxPayload[202];  // MATLAB outputs 202 info bits (not 250)
    int errs;
    boolean_T rxStatus;

    // Call MATLAB Coder generated function
    printf("Calling MATLAB dsss_receiver...\n");
    dsss_receiver(otaBuffer, &settings, rxPayload, &errs, &rxStatus);

    // Check results
    printf("\n=== MATLAB Results ===\n");
    printf("rxStatus: %s\n", rxStatus ? "SUCCESS" : "FAILED");
    printf("BCH Errors: %d\n", errs);

    if (!rxStatus) {
        fprintf(stderr, "ERROR: MATLAB receiver failed to decode signal\n");
        return -1;
    }

    // Convert MATLAB output to our format
    // MATLAB gives 202 information bits (bits 0-201)
    // BCH(250,202) adds 48 parity bits (202-249)
    // We only copy the 202 info bits, rest stays zero

    printf("Converting %d information bits to output...\n", 202);
    for (int i = 0; i < 202; i++) {
        output_bits[i] = rxPayload[i] ? 1 : 0;
    }

    // Fill remaining bits with zeros (parity bits not provided by MATLAB)
    for (int i = 202; i < 250; i++) {
        output_bits[i] = 0;
    }

    printf("SUCCESS: Decoded 202 information bits with %d BCH errors\n", errs);

    return 0;
}

/**
 * @brief Generate PRN sequence (stub - MATLAB handles this internally)
 */
void dsss_generate_prn(uint32_t initial_state, int8_t *output, size_t num_chips) {
    (void)initial_state;
    (void)output;
    (void)num_chips;
    fprintf(stderr, "WARNING: dsss_generate_prn() not implemented in MATLAB version\n");
}

/**
 * @brief Generate PRN I/Q sequences (stub - MATLAB handles this internally)
 */
void dsss_generate_prn_sequences(int8_t *prn_i, int8_t *prn_q) {
    (void)prn_i;
    (void)prn_q;
    fprintf(stderr, "WARNING: dsss_generate_prn_sequences() not implemented in MATLAB version\n");
}
