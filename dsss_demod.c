/**
 * @file dsss_demod.c
 * @brief DSSS/OQPSK Demodulator for COSPAS-SARSAT 2G Beacons (T.018)
 *
 * MATLAB-Compliant Implementation
 * Reference: DSSSReceiverForSARbasedTrackingSystem.pdf (MathWorks R2024a)
 *
 * @date 2025-01-11
 * @version 11.0 (complete rewrite from V10.2)
 */

#include "dsss_demod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =============================================================================
// FARROW INTERPOLATOR (Piecewise Parabolic, �=0.5)
// =============================================================================

/**
 * @brief Farrow piecewise parabolic interpolator
 *
 * Interpolates a sample at fractional position �  [0,1) using 4 samples.
 * Uses piecewise parabolic polynomial with �=0.5 (MATLAB default).
 *
 * Reference: MATLAB comm.SymbolSynchronizer documentation
 *
 * @param samples 4 consecutive samples: [x[n-1], x[n], x[n+1], x[n+2]]
 * @param mu Fractional interval  [0,1)
 * @return Interpolated sample at position �
 */
static inline float complex farrow_interpolate(const float complex samples[4], float mu) {
    // Farrow structure with �=0.5 (piecewise parabolic)
    // y(�) = c0 + c1*� + c2*�� + c3*��

    float complex x0 = samples[0];  // x[n-1]
    float complex x1 = samples[1];  // x[n]
    float complex x2 = samples[2];  // x[n+1]
    float complex x3 = samples[3];  // x[n+2]

    // Piecewise parabolic coefficients (�=0.5)
    float complex c0 = x1;
    float complex c1 = 0.5f * (x2 - x0);
    float complex c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    float complex c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);

    // Horner's method: y = c0 + �(c1 + �(c2 + �*c3))
    return c0 + mu * (c1 + mu * (c2 + mu * c3));
}

// =============================================================================
// ZERO-CROSSING TED (Decision-Directed)
// =============================================================================

/**
 * @brief Zero-Crossing Timing Error Detector
 *
 * Computes timing error using Zero-Crossing method (MATLAB page 16):
 *   e(k) = x_mid((k-1/2)Ts)[�_I(k-1) - �_I(k)] +
 *          y_mid((k-1/2)Ts)[�_Q(k-1) - �_Q(k)]
 *
 * Where:
 *   - x_mid, y_mid: I/Q components of mid-symbol sample
 *   - �_I, �_Q: hard decisions on I/Q components
 *
 * @param current_symbol Current symbol x(kTs)
 * @param mid_symbol Mid-point sample x((k-1/2)Ts)
 * @param prev_mid_symbol Previous mid-point x((k-3/2)Ts)
 * @param prev_i_decision Previous I hard decision �_I(k-1)
 * @param prev_q_decision Previous Q hard decision �_Q(k-1)
 * @return Timing error estimate
 */
static inline float zero_crossing_ted(float complex current_symbol,
                                      float complex mid_symbol,
                                      float complex prev_mid_symbol,
                                      float prev_i_decision,
                                      float prev_q_decision) {
    // Current hard decisions
    float curr_i_decision = (crealf(current_symbol) > 0) ? 1.0f : -1.0f;
    float curr_q_decision = (cimagf(current_symbol) > 0) ? 1.0f : -1.0f;

    // Previous mid-point I/Q components
    float prev_mid_i = crealf(prev_mid_symbol);
    float prev_mid_q = cimagf(prev_mid_symbol);

    // Zero-Crossing TED formula
    float timing_error = prev_mid_i * (prev_i_decision - curr_i_decision) +
                        prev_mid_q * (prev_q_decision - curr_q_decision);

    return timing_error;
}

// =============================================================================
// TIMING RECOVERY INITIALIZATION
// =============================================================================

/**
 * @brief Initialize timing recovery PLL
 *
 * Calculates loop filter gains using MATLAB formulas:
 *   � = (B_n*T_s / N_sps) / (� + 1/(4�))
 *   K1 = (-4��) / ((1 + 2�� + ��) * K_p)
 *   K2 = (-4��) / ((1 + 2�� + ��) * K_p)
 */
int timing_recovery_init(timing_recovery_state_t *state,
                        int sps,
                        float normalized_loop_bw,
                        float damping_factor,
                        float detector_gain) {
    if (!state || sps < 2) {
        fprintf(stderr, "ERROR: Invalid timing recovery parameters\n");
        return -1;
    }

    memset(state, 0, sizeof(timing_recovery_state_t));

    // Store PLL parameters
    state->samples_per_symbol = sps;
    state->normalized_loop_bw = normalized_loop_bw;
    state->damping_factor = damping_factor;
    state->detector_gain = detector_gain;

    // Calculate loop filter gains (MATLAB formulas)
    float theta = (normalized_loop_bw / sps) / (damping_factor + 1.0f / (4.0f * damping_factor));
    float denom = (1.0f + 2.0f * damping_factor * theta + theta * theta) * detector_gain;

    state->k1 = (-4.0f * damping_factor * theta) / denom;
    state->k2 = (-4.0f * theta * theta) / denom;

    printf("Timing Recovery PLL Initialization:\n");
    printf("  Samples per symbol: %d\n", sps);
    printf("  Normalized loop BW: %.6f\n", normalized_loop_bw);
    printf("  Damping factor: %.2f\n", damping_factor);
    printf("  Detector gain: %.2f\n", detector_gain);
    printf("  Calculated gains: K1=%.6f, K2=%.6f\n", state->k1, state->k2);

    // Initialize interpolation controller
    state->mu = 0.0f;
    state->strobe = 0.0f;

    // Initialize TED state
    state->first_symbol = 1;
    state->prev_mid_sample = 0.0f;
    state->prev_i_decision = 0.0f;
    state->prev_q_decision = 0.0f;

    // Initialize interpolator buffer
    state->buf_write_idx = 0;
    for (int i = 0; i < 8; i++) {
        state->buffer[i] = 0.0f;
    }

    // Allocate Q-channel delay buffer (sps/2 samples for OQPSK)
    state->q_delay_size = sps / 2;
    state->q_delay_buffer = calloc(state->q_delay_size, sizeof(float complex));
    if (!state->q_delay_buffer) {
        fprintf(stderr, "ERROR: Failed to allocate Q delay buffer\n");
        return -1;
    }
    state->q_delay_idx = 0;

    printf("  Q-channel delay: %d samples (OQPSK Tc/2 offset)\n", state->q_delay_size);

    return 0;
}

// =============================================================================
// TIMING RECOVERY PROCESSING
// =============================================================================

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
 *    a. Interpolate symbol at fractional position �
 *    b. Interpolate mid-symbol for Zero-Crossing TED
 *    c. Compute timing error (if not first symbol)
 *    d. Update loop filter (PI)
 *    e. Update � from loop filter output
 */
int timing_recovery_process(timing_recovery_state_t *state,
                           const float complex *input,
                           size_t input_len,
                           float complex *output,
                           size_t *output_len) {
    if (!state || !input || !output || !output_len) {
        fprintf(stderr, "ERROR: Invalid timing recovery parameters\n");
        return -1;
    }

    size_t out_idx = 0;
    int sps = state->samples_per_symbol;

    for (size_t i = 0; i < input_len; i++) {
        float complex sample = input[i];

        // Apply Q-channel delay for OQPSK (Tc/2 offset)
        float complex delayed_sample = sample;
        if (state->q_delay_size > 0) {
            // Extract I and Q components
            float i_comp = crealf(sample);
            float q_comp_delayed = cimagf(state->q_delay_buffer[state->q_delay_idx]);

            // Store current Q for future use
            state->q_delay_buffer[state->q_delay_idx] = sample;
            state->q_delay_idx = (state->q_delay_idx + 1) % state->q_delay_size;

            // Reconstruct with delayed Q
            delayed_sample = i_comp + I * q_comp_delayed;
        }

        // Store in circular interpolation buffer
        state->buffer[state->buf_write_idx] = delayed_sample;
        state->buf_write_idx = (state->buf_write_idx + 1) % 8;

        // Update strobe counter
        state->strobe += 1.0f;

        // Check if symbol timing reached
        if (state->strobe >= sps) {
            state->strobe -= sps;

            // Prepare 4 samples for Farrow interpolator
            // We need [x[n-1], x[n], x[n+1], x[n+2]] where x[n+1] is "current"
            float complex farrow_samples[4];
            int base_idx = (state->buf_write_idx - 3 + 8) % 8;
            for (int j = 0; j < 4; j++) {
                farrow_samples[j] = state->buffer[(base_idx + j) % 8];
            }

            // Interpolate symbol at fractional position �
            float complex symbol = farrow_interpolate(farrow_samples, state->mu);

            // Interpolate mid-symbol for TED (� + 0.5)
            float mu_mid = state->mu + 0.5f;
            if (mu_mid >= 1.0f) {
                mu_mid -= 1.0f;
                // Use next set of samples
                base_idx = (base_idx + 1) % 8;
                for (int j = 0; j < 4; j++) {
                    farrow_samples[j] = state->buffer[(base_idx + j) % 8];
                }
            }
            float complex mid_symbol = farrow_interpolate(farrow_samples, mu_mid);

            // Compute timing error using Zero-Crossing TED
            float timing_error = 0.0f;
            if (!state->first_symbol) {
                timing_error = zero_crossing_ted(symbol,
                                                 mid_symbol,
                                                 state->prev_mid_sample,
                                                 state->prev_i_decision,
                                                 state->prev_q_decision);
            } else {
                state->first_symbol = 0;
            }

            // Update TED state for next iteration
            state->prev_mid_sample = mid_symbol;
            state->prev_i_decision = (crealf(symbol) > 0) ? 1.0f : -1.0f;
            state->prev_q_decision = (cimagf(symbol) > 0) ? 1.0f : -1.0f;

            // Update loop filter (PI)
            float proportional = state->k1 * timing_error;
            state->integrator += state->k2 * timing_error;
            float loop_filter_out = proportional + state->integrator;

            // Update fractional interval � (modulo-1 counter)
            state->mu += loop_filter_out;

            // Keep � in [0, 1) range
            while (state->mu >= 1.0f) {
                state->mu -= 1.0f;
                state->strobe += 1.0f;  // Advance strobe when � wraps
            }
            while (state->mu < 0.0f) {
                state->mu += 1.0f;
                state->strobe -= 1.0f;  // Retard strobe when � wraps negative
            }

            // Output symbol
            output[out_idx++] = symbol;
        }
    }

    *output_len = out_idx;
    return 0;
}

// =============================================================================
// TIMING RECOVERY CLEANUP
// =============================================================================

void timing_recovery_free(timing_recovery_state_t *state) {
    if (state) {
        if (state->q_delay_buffer) {
            free(state->q_delay_buffer);
            state->q_delay_buffer = NULL;
        }
    }
}

// =============================================================================
// PRN SEQUENCE GENERATION
// =============================================================================

/**
 * @brief Generate PRN sequence using T.018 LFSR
 *
 * LFSR: x^23 + x^18 + 1
 * RIGHT shift, LSB output
 */
void dsss_generate_prn_sequences(int8_t *prn_i, int8_t *prn_q) {
    // Initial states from T.018 Table 2.2
    uint32_t lfsr_i = 0x000001;  // Normal I
    uint32_t lfsr_q = 0x1AC1FC;  // Normal Q

    const uint32_t mask = 0x7FFFFF;  // 23-bit mask

    for (int i = 0; i < DSSS_PACKET_CHIPS; i++) {
        // Extract LSB for output
        prn_i[i] = (lfsr_i & 1) ? -1 : 1;  // Convert 0�+1, 1�-1
        prn_q[i] = (lfsr_q & 1) ? -1 : 1;

        // Compute feedback (XOR of bit 23 and bit 18)
        uint32_t fb_i = ((lfsr_i >> 22) ^ (lfsr_i >> 17)) & 1;
        uint32_t fb_q = ((lfsr_q >> 22) ^ (lfsr_q >> 17)) & 1;

        // Right shift and insert feedback at MSB
        lfsr_i = ((lfsr_i >> 1) | (fb_i << 22)) & mask;
        lfsr_q = ((lfsr_q >> 1) | (fb_q << 22)) & mask;
    }
}

// =============================================================================
// AUTOMATIC GAIN CONTROL (AGC)
// =============================================================================

typedef struct {
    float gain;
    float avg_power;
    float step_size;
    int averaging_length;
    float max_gain;
} agc_t;

/**
 * @brief Initialize AGC with specified parameters
 */
static void agc_init(agc_t *agc, float step_size, int avg_length, float max_gain) {
    agc->gain = 1.0f;
    agc->avg_power = 0.0f;
    agc->step_size = step_size;
    agc->averaging_length = avg_length;
    agc->max_gain = max_gain;
}

/**
 * @brief Process samples through AGC to normalize power to unity
 */
static void agc_process(agc_t *agc, const float complex *input,
                       float complex *output, size_t length) {
    for (size_t i = 0; i < length; i++) {
        // Apply current gain
        output[i] = input[i] * agc->gain;

        // Measure instantaneous power
        float inst_power = crealf(output[i]) * crealf(output[i]) +
                          cimagf(output[i]) * cimagf(output[i]);

        // Update average power with exponential smoothing
        float alpha = 1.0f / agc->averaging_length;
        agc->avg_power = (1.0f - alpha) * agc->avg_power + alpha * inst_power;

        // Update gain to maintain unit power (target = 1.0)
        if (agc->avg_power > 1e-10f) {
            float desired_gain = sqrtf(1.0f / agc->avg_power);
            agc->gain += agc->step_size * (desired_gain - agc->gain);

            // Limit maximum gain
            if (agc->gain > agc->max_gain) {
                agc->gain = agc->max_gain;
            }
        }
    }
}

/**
 * @brief Saturate signal to prevent excessive values during correlation
 */
static void saturate_signal(float complex *signal, size_t length, float limit) {
    for (size_t i = 0; i < length; i++) {
        float mag = cabsf(signal[i]);
        if (mag > limit) {
            signal[i] = signal[i] * (limit / mag);
        }
    }
}

// =============================================================================
// POLYPHASE CORRELATOR FOR PREAMBLE DETECTION
// =============================================================================

/**
 * @brief Polyphase correlator - correlates reference signal across all sample phases
 * @param signal Input signal buffer
 * @param sig_len Length of input signal
 * @param reference Reference signal (preamble)
 * @param ref_len Length of reference
 * @param sps Samples per symbol
 * @param corr_output Correlation output buffer
 * @return Index of maximum correlation peak, or -1 if not found
 */
/**
 * @brief timingEstimate - Cross-correlation with threshold detection
 *
 * MATLAB equivalent of the timingEstimate function used in helperPolyphaseCorrelator
 * Returns the index of maximum correlation if it exceeds threshold
 *
 * @param signal Decimated signal for one phase
 * @param sig_len Signal length
 * @param reference Reference signal
 * @param ref_len Reference length
 * @param xcorr_out Output buffer for full correlation (size: sig_len + ref_len - 1)
 * @param threshold Detection threshold
 * @return Index of correlation peak (1-indexed like MATLAB), or -1 if not found
 */
static int timingEstimate(const float complex *signal, size_t sig_len,
                         const float complex *reference, size_t ref_len,
                         float complex *xcorr_out, float threshold) {
    // MATLAB doc page 9: Normalized cross-correlation using FFT
    size_t xcorr_len = sig_len + ref_len - 1;

    // Find next power of 2 for FFT efficiency
    size_t fft_size = 1;
    while (fft_size < xcorr_len) fft_size <<= 1;

    // Calculate reference signal power
    float refSigPower = 0.0f;
    for (size_t i = 0; i < ref_len; i++) {
        refSigPower += crealf(reference[i] * conjf(reference[i]));
    }

    // Allocate FFT buffers
    fftwf_complex *sig_fft = fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    fftwf_complex *ref_fft = fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    fftwf_complex *corr_fft = fftwf_malloc(sizeof(fftwf_complex) * fft_size);

    float *sig_fft_f = (float*)sig_fft;
    float *ref_fft_f = (float*)ref_fft;
    float *corr_fft_f = (float*)corr_fft;

    // Prepare signal for FFT (zero-pad)
    for (size_t i = 0; i < sig_len; i++) {
        sig_fft_f[2*i] = crealf(signal[i]);
        sig_fft_f[2*i+1] = cimagf(signal[i]);
    }
    for (size_t i = sig_len; i < fft_size; i++) {
        sig_fft_f[2*i] = 0.0f;
        sig_fft_f[2*i+1] = 0.0f;
    }

    // Prepare reference for FFT (zero-pad and flip for correlation)
    for (size_t i = 0; i < ref_len; i++) {
        ref_fft_f[2*i] = crealf(conjf(reference[ref_len - 1 - i]));
        ref_fft_f[2*i+1] = cimagf(conjf(reference[ref_len - 1 - i]));
    }
    for (size_t i = ref_len; i < fft_size; i++) {
        ref_fft_f[2*i] = 0.0f;
        ref_fft_f[2*i+1] = 0.0f;
    }

    // FFT of both signals
    fftwf_plan plan_sig = fftwf_plan_dft_1d(fft_size, sig_fft, sig_fft, FFTW_FORWARD, FFTW_ESTIMATE);
    fftwf_plan plan_ref = fftwf_plan_dft_1d(fft_size, ref_fft, ref_fft, FFTW_FORWARD, FFTW_ESTIMATE);
    fftwf_execute(plan_sig);
    fftwf_execute(plan_ref);

    // Multiply in frequency domain
    for (size_t i = 0; i < fft_size; i++) {
        float re = sig_fft_f[2*i] * ref_fft_f[2*i] - sig_fft_f[2*i+1] * ref_fft_f[2*i+1];
        float im = sig_fft_f[2*i] * ref_fft_f[2*i+1] + sig_fft_f[2*i+1] * ref_fft_f[2*i];
        corr_fft_f[2*i] = re;
        corr_fft_f[2*i+1] = im;
    }

    // IFFT to get correlation
    fftwf_plan plan_ifft = fftwf_plan_dft_1d(fft_size, corr_fft, corr_fft, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftwf_execute(plan_ifft);

    // Normalize by FFT size and compute sliding window power
    float *sigMagSq = malloc(sig_len * sizeof(float));
    for (size_t i = 0; i < sig_len; i++) {
        sigMagSq[i] = crealf(signal[i] * conjf(signal[i]));
    }

    float *waveformMagSq = calloc(xcorr_len, sizeof(float));
    for (size_t lag = 0; lag < xcorr_len; lag++) {
        for (size_t i = 0; i < ref_len; i++) {
            int sig_idx = (int)lag - (int)i;
            if (sig_idx >= 0 && sig_idx < (int)sig_len) {
                waveformMagSq[lag] += sigMagSq[sig_idx];
            }
        }
    }

    // Find maximum normalized correlation
    float max_corr = 0.0f;
    int max_idx = -1;

    for (size_t lag = 0; lag < xcorr_len; lag++) {
        // Extract correlation value
        float complex corr = (corr_fft_f[2*lag] + I * corr_fft_f[2*lag+1]) / (float)fft_size;

        // Normalize
        float normFactor = sqrtf(waveformMagSq[lag] * refSigPower);
        float normCorr = (normFactor > 1e-10f) ? (cabsf(corr) / normFactor) : 0.0f;

        xcorr_out[lag] = normCorr + 0.0f * I;

        if (normCorr > max_corr) {
            max_corr = normCorr;
            max_idx = lag;
        }
    }

    // Cleanup
    fftwf_destroy_plan(plan_sig);
    fftwf_destroy_plan(plan_ref);
    fftwf_destroy_plan(plan_ifft);
    fftwf_free(sig_fft);
    fftwf_free(ref_fft);
    fftwf_free(corr_fft);
    free(sigMagSq);
    free(waveformMagSq);

    if (max_corr < threshold) {
        return -1;
    }

    return max_idx + 1;
}

/**
 * @brief helperPolyphaseCorrelator - Exact MATLAB translation
 *
 * Line-by-line translation of helperPolyphaseCorrelator.m
 */
static int polyphase_correlator(const float complex *signal, size_t sig_len,
                                const float complex *reference, size_t ref_len,
                                int sps, float complex *corr_output,
                                int offset) {
    // MATLAB: decimatedSampleBuffer = reshape(rxBuffer,sps,[]);
    size_t decimated_len = sig_len / sps;

    // MATLAB: bufferLen = length(decimatedSampleBuffer);
    // In MATLAB, length() returns max dimension, so for [sps x N], it's N
    size_t bufferLen = decimated_len;

    // MATLAB: xcorrBuffer = zeros(bufferLen+length(referenceSignal)-1,sps);
    size_t xcorr_len = bufferLen + ref_len - 1;
    float complex **xcorrBuffer = malloc(sps * sizeof(float complex*));
    for (int k = 0; k < sps; k++) {
        xcorrBuffer[k] = malloc(xcorr_len * sizeof(float complex));
    }

    // MATLAB: startIdxs = [];
    int *startIdxs = malloc(sps * sizeof(int));
    for (int k = 0; k < sps; k++) {
        startIdxs[k] = -1;  // -1 means empty in C
    }

    // MATLAB: for k=1:sps
    for (int k = 0; k < sps; k++) {  // k is 0-indexed in C, 1-indexed in MATLAB
        // Extract decimatedSampleBuffer(k,:) - samples at [k, k+sps, k+2*sps, ...]
        float complex *decimated_phase = malloc(decimated_len * sizeof(float complex));
        for (size_t i = 0; i < decimated_len; i++) {
            decimated_phase[i] = signal[k + i * sps];
        }

        // MATLAB: [idx2,xcorrBuffer(:,k)] = timingEstimate(decimatedSampleBuffer(k,:).',referenceSignal,Threshold=0.35);
        int idx2 = timingEstimate(decimated_phase, decimated_len, reference, ref_len,
                                  xcorrBuffer[k], 0.35f);

        // MATLAB: if ~isempty(idx2)
        if (idx2 > 0) {
            // MATLAB: startIdxs(k) = idx2 - offset + 1;
            // idx2 is already 1-indexed from timingEstimate
            startIdxs[k] = idx2 - offset + 1;
        }

        free(decimated_phase);
    }

    // MATLAB: [maxXcorrVals,maxXcorrIdxs] = max(abs(xcorrBuffer));
    float *maxXcorrVals = malloc(sps * sizeof(float));
    for (int k = 0; k < sps; k++) {
        maxXcorrVals[k] = 0.0f;
        for (size_t i = 0; i < xcorr_len; i++) {
            float mag = cabsf(xcorrBuffer[k][i]);
            if (mag > maxXcorrVals[k]) {
                maxXcorrVals[k] = mag;
            }
        }
    }

    // MATLAB: [maxDetectorVal,kidx] = max(maxXcorrVals);
    float maxDetectorVal = 0.0f;
    int kidx = -1;
    for (int k = 0; k < sps; k++) {
        if (maxXcorrVals[k] > maxDetectorVal) {
            maxDetectorVal = maxXcorrVals[k];
            kidx = k;
        }
    }

    int final_idx = -1;

    if (kidx >= 0) {
        // MATLAB: corrBuffer = xcorrBuffer(length(referenceSignal):end,kidx);
        size_t corrBuffer_len = xcorr_len - ref_len + 1;
        float *corrBuffer_mag = malloc(corrBuffer_len * sizeof(float));
        for (size_t i = 0; i < corrBuffer_len; i++) {
            corrBuffer_mag[i] = cabsf(xcorrBuffer[kidx][ref_len - 1 + i]);
        }

        // MATLAB: if maxDetectorVal < 5.5*mean(abs(corrBuffer))
        float mean_corr = 0.0f;
        for (size_t i = 0; i < corrBuffer_len; i++) {
            mean_corr += corrBuffer_mag[i];
        }
        mean_corr /= corrBuffer_len;

        if (maxDetectorVal >= 5.5f * mean_corr) {
            // MATLAB: startIdx = startIdxs(kidx);
            int startIdx = startIdxs[kidx];

            if (startIdx > 0) {
                // MATLAB: idx = max(1,(startIdx-1)*sps + kidx - (sps/2));
                // kidx is 0-indexed in C, but formula needs 1-indexed
                int idx_matlab = (startIdx - 1) * sps + (kidx + 1) - (sps / 2);
                if (idx_matlab < 1) idx_matlab = 1;

                // Convert to 0-indexed for C
                final_idx = idx_matlab - 1;

                printf("Found preamble at correlation buffer number %d, index %d, sample index %d\n",
                       kidx + 1, startIdx, idx_matlab);
            }
        }

        free(corrBuffer_mag);
    }

    // Cleanup
    for (int k = 0; k < sps; k++) {
        free(xcorrBuffer[k]);
    }
    free(xcorrBuffer);
    free(startIdxs);
    free(maxXcorrVals);

    return final_idx;
}

// =============================================================================
// FREQUENCY OFFSET ESTIMATION AND CORRECTION
// =============================================================================

/**
 * @brief Coarse frequency offset estimator for OQPSK
 * Uses FFT-based method on 4th power of signal to remove modulation
 */
static float estimate_coarse_frequency_offset(const float complex *signal,
                                              size_t length,
                                              float fs) {
    // Raise signal to 4th power to remove QPSK modulation
    float complex *signal_4th = malloc(length * sizeof(float complex));
    for (size_t i = 0; i < length; i++) {
        float complex s = signal[i];
        float complex s2 = s * s;
        signal_4th[i] = s2 * s2;
    }

    // Compute FFT
    size_t fft_size = length;
    fftwf_complex *fft_in = (fftwf_complex*)signal_4th;
    fftwf_complex *fft_out = fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    fftwf_plan plan = fftwf_plan_dft_1d(fft_size, fft_in, fft_out,
                                        FFTW_FORWARD, FFTW_ESTIMATE);
    fftwf_execute(plan);

    // Find peak in FFT (excluding DC)
    float max_mag = 0.0f;
    int max_bin = 0;

    for (size_t i = 1; i < fft_size / 2; i++) {
        float re = ((float*)fft_out)[2*i];
        float im = ((float*)fft_out)[2*i+1];
        float mag = sqrtf(re*re + im*im);

        if (mag > max_mag) {
            max_mag = mag;
            max_bin = i;
        }
    }

    // Convert bin to frequency and divide by 4 (due to 4th power)
    float freq_offset = (max_bin * fs / fft_size) / 4.0f;

    // Handle negative frequencies (wrapped around)
    if (freq_offset > fs / 8.0f) {
        freq_offset -= fs / 4.0f;
    }

    fftwf_destroy_plan(plan);
    fftwf_free(fft_out);
    free(signal_4th);

    return freq_offset;
}

/**
 * @brief Apply frequency offset correction to signal
 */
static void apply_frequency_offset(float complex *signal, size_t length,
                                  float fs, float freq_offset) {
    for (size_t i = 0; i < length; i++) {
        float phase = -2.0f * M_PI * freq_offset * i / fs;
        float complex correction = cosf(phase) + I * sinf(phase);
        signal[i] *= correction;
    }
}

// =============================================================================
// CARRIER SYNCHRONIZATION (FINE FREQUENCY/PHASE TRACKING)
// =============================================================================

typedef struct {
    float phase;
    float frequency;
    float loop_bw;
    float damping;
    int sps;
    float k1;  // Proportional gain
    float k2;  // Integral gain
} carrier_sync_t;

/**
 * @brief Initialize carrier synchronizer (Costas loop for OQPSK)
 */
static void carrier_sync_init(carrier_sync_t *sync, float loop_bw,
                             float damping, int sps) {
    sync->phase = 0.0f;
    sync->frequency = 0.0f;
    sync->loop_bw = loop_bw;
    sync->damping = damping;
    sync->sps = sps;

    // Calculate loop filter coefficients
    float theta = loop_bw / (damping + 1.0f / (4.0f * damping));
    float d = 1.0f + 2.0f * damping * theta + theta * theta;
    sync->k1 = (4.0f * damping * theta) / d;
    sync->k2 = (4.0f * theta * theta) / d;
}

/**
 * @brief Process signal through carrier synchronizer
 */
static void carrier_sync_process(carrier_sync_t *sync, const float complex *input,
                                float complex *output, size_t length,
                                float *phase_error) {
    for (size_t i = 0; i < length; i++) {
        // Apply phase rotation
        float complex rotation = cosf(-sync->phase) + I * sinf(-sync->phase);
        output[i] = input[i] * rotation;

        // Phase error detector (decision-directed for QPSK)
        // Use every sps-th sample for symbol-rate processing
        if (i % sync->sps == 0) {
            float re = crealf(output[i]);
            float im = cimagf(output[i]);

            // Hard decisions
            float re_decided = (re > 0) ? 1.0f : -1.0f;
            float im_decided = (im > 0) ? 1.0f : -1.0f;

            // Phase error = cross product
            float error = re * im_decided - im * re_decided;

            if (phase_error) {
                phase_error[i] = error;
            }

            // Update loop filter
            sync->frequency += sync->k2 * error;
            sync->phase += sync->frequency + sync->k1 * error;

            // Wrap phase to [-pi, pi]
            while (sync->phase > M_PI) sync->phase -= 2.0f * M_PI;
            while (sync->phase < -M_PI) sync->phase += 2.0f * M_PI;
        }
    }
}

// =============================================================================
// QPSK DEMODULATION AND PHASE AMBIGUITY RESOLUTION
// =============================================================================

/**
 * @brief Demodulate QPSK symbols to bits
 * @param symbols Input QPSK symbols
 * @param length Number of symbols
 * @param bits Output bits (I and Q interleaved)
 * @param phase_rotation Phase rotation to apply (0, 1, 2, 3 for 0, 90, 180, 270 deg)
 */
static void qpsk_demod(const float complex *symbols, size_t length,
                      uint8_t *bits, int phase_rotation) {
    float complex rotation = cexpf(I * phase_rotation * M_PI / 2.0f);

    for (size_t i = 0; i < length; i++) {
        float complex rotated = symbols[i] * rotation;
        float re = crealf(rotated);
        float im = cimagf(rotated);

        // QPSK demodulation: I and Q hard decisions
        bits[2*i] = (re > 0.0f) ? 0 : 1;
        bits[2*i + 1] = (im > 0.0f) ? 0 : 1;
    }
}

// =============================================================================
// MAIN RECEIVER FUNCTION
// =============================================================================

/**
 * @brief Complete DSSS OQPSK receiver processing chain (V11.0)
 *
 * Integrates V10.2 components with new MATLAB-compliant timing recovery.
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits) {

    printf("=== DSSS OQPSK Receiver V11.0 Start ===\n");
    printf("Buffer length: %zu samples\n", buffer_length);
    printf("Sampling rate: %.0f Hz, SPS: %d\n", fs, sps);
    printf("Max Doppler: %d Hz\n", max_doppler);

    // Calculate burst size
    size_t num_burst_samples = (DSSS_TOTAL_BITS / 2) * DSSS_SPREADING_FACTOR * sps;
    size_t num_preamble_chips = DSSS_PREAMBLE_LENGTH * (DSSS_SPREADING_FACTOR / 2);

    // Generate PRN sequences
    int8_t *prn_i = malloc(DSSS_PACKET_CHIPS);
    int8_t *prn_q = malloc(DSSS_PACKET_CHIPS);
    dsss_generate_prn_sequences(prn_i, prn_q);
    printf("Generated PRN sequences\n");

    // Preamble detection parameters
    const int preamble_detection_offset = 200;
    const int preamble_detection_length = 175;

    // Create reference preamble for detection (QPSK at symbol rate)
    float complex *preamble_qpsk = malloc(num_preamble_chips / 2 * sizeof(float complex));
    for (size_t i = 0; i < num_preamble_chips / 2; i++) {
        float complex chip_i = (prn_i[i] > 0) ? 1.0f : -1.0f;
        float complex chip_q = (prn_q[i] > 0) ? 1.0f : -1.0f;
        preamble_qpsk[i] = (chip_i + I * chip_q) * cexpf(I * M_PI / 4.0f);
    }

    // Extract shortened preamble for detection
    float complex *preamble_detect = malloc(preamble_detection_length * sizeof(float complex));
    memcpy(preamble_detect,
           preamble_qpsk + preamble_detection_offset,
           preamble_detection_length * sizeof(float complex));

    // Double buffering (ping-pong scheme)
    size_t num_buffers = buffer_length / num_burst_samples;
    float complex *sample_buffer = malloc(num_burst_samples * 2 * sizeof(float complex));

    // Initialize with noise
    for (size_t i = 0; i < num_burst_samples * 2; i++) {
        float re = ((float)rand() / RAND_MAX - 0.5f) * 0.1f;
        float im = ((float)rand() / RAND_MAX - 0.5f) * 0.1f;
        sample_buffer[i] = re + I * im;
    }

    int preamble_idx = -1;
    float complex *corr_buffer = malloc(num_burst_samples * sizeof(float complex));

    printf("Processing %zu samples in %zu buffers (%.2f sec/buffer)\n",
           buffer_length, num_buffers, (float)num_burst_samples / fs);

    // Loop through buffers (continuous reception)
    for (size_t n = 0; n < num_buffers; n++) {
        // Shift the last burst samples to front of buffer
        memmove(sample_buffer,
                sample_buffer + num_burst_samples,
                num_burst_samples * sizeof(float complex));

        // Read in next burst
        size_t next_burst_idx = n * num_burst_samples;
        if (next_burst_idx + num_burst_samples <= buffer_length) {
            memcpy(sample_buffer + num_burst_samples,
                   ota_buffer + next_burst_idx,
                   num_burst_samples * sizeof(float complex));
        }

        // Configure and process AGC
        agc_t agc;
        agc_init(&agc, 0.01f, 10 * sps, 1024.0f);

        float complex *rx_agc_samples = malloc(num_burst_samples * 2 * sizeof(float complex));
        agc_process(&agc, sample_buffer, rx_agc_samples, num_burst_samples * 2);

        // Saturate signals to prevent false preamble detection
        saturate_signal(rx_agc_samples, num_burst_samples * 2, 1.2f);

        if (n == 0) {
            printf("AGC: gain=%.2f, avg_power=%.6f\n", agc.gain, agc.avg_power);
        }

        // Convert from OQPSK to QPSK for symbol-based preamble detection
        float complex *sample_buffer_qpsk = malloc(num_burst_samples * 2 * sizeof(float complex));
        int q_delay = sps / 2;
        for (size_t i = 0; i < num_burst_samples * 2 - q_delay; i++) {
            sample_buffer_qpsk[i] = crealf(rx_agc_samples[i]) +
                                   I * cimagf(rx_agc_samples[i + q_delay]);
        }
        for (size_t i = num_burst_samples * 2 - q_delay; i < num_burst_samples * 2; i++) {
            sample_buffer_qpsk[i] = 0.0f;
        }

        // Detect preamble across different frequency offsets
        for (int preamble_freq_offset = -max_doppler;
             preamble_freq_offset <= max_doppler;
             preamble_freq_offset += 150) {

            // Add frequency offset to the preamble
            float complex *preamble_shifted = malloc(preamble_detection_length * sizeof(float complex));
            for (int i = 0; i < preamble_detection_length; i++) {
                float phase = 2.0f * M_PI * preamble_freq_offset * i / DSSS_CHIP_RATE;
                preamble_shifted[i] = preamble_detect[i] * cexpf(I * phase);
            }

            // Get start index of preamble (search only first half)
            int start_samp_idx = polyphase_correlator(
                sample_buffer_qpsk, num_burst_samples,
                preamble_shifted, preamble_detection_length,
                sps, corr_buffer, preamble_detection_offset);

            free(preamble_shifted);

            if (start_samp_idx >= 0) {
                float corr_peak = cabsf(corr_buffer[start_samp_idx]);
                printf("Preamble found: buffer=%zu, idx=%d, freq_offset=%d Hz, corr=%.3f\n",
                       n + 1, start_samp_idx, preamble_freq_offset, corr_peak);
                preamble_idx = start_samp_idx;
                free(sample_buffer_qpsk);
                free(rx_agc_samples);
                goto preamble_found;
            }
        }

        free(sample_buffer_qpsk);
        free(rx_agc_samples);
    }

preamble_found:
    if (preamble_idx < 0) {
        printf("ERROR: Preamble not detected\n");
        free(sample_buffer);
        free(preamble_qpsk);
        free(preamble_detect);
        free(prn_i);
        free(prn_q);
        free(corr_buffer);
        return -2;
    }

    // Extract transmission burst from sample buffer
    size_t burst_length = (DSSS_PACKET_CHIPS + 20) * sps;
    float complex *rx_burst = malloc(burst_length * sizeof(float complex));
    memcpy(rx_burst, sample_buffer + preamble_idx, burst_length * sizeof(float complex));

    printf("Burst extracted, length: %zu samples (%.3f sec)\n",
           burst_length, (float)burst_length / fs);

    // Step 1 - Coarse Frequency Offset Estimation and Correction
    float coarse_offset = estimate_coarse_frequency_offset(rx_burst, burst_length, fs);
    printf("Estimated coarse frequency offset = %.3f kHz\n", coarse_offset / 1000.0f);

    float complex *coarse_sync_out = malloc(burst_length * sizeof(float complex));
    apply_frequency_offset(rx_burst, burst_length, fs, coarse_offset);
    memcpy(coarse_sync_out, rx_burst, burst_length * sizeof(float complex));

    // Step 2 - Fine Frequency Correction (Carrier Synchronizer)
    carrier_sync_t carrier_sync;
    carrier_sync_init(&carrier_sync, 0.01f, 0.707f, sps);

    float complex *carrier_sync_out = malloc(burst_length * sizeof(float complex));
    float *phase_err = malloc(burst_length * sizeof(float));

    carrier_sync_process(&carrier_sync, coarse_sync_out, carrier_sync_out, burst_length, phase_err);
    printf("Carrier synchronization complete\n");

    // Fine preamble detection after frequency correction
    float complex *carrier_sync_out_qpsk = malloc(burst_length * sizeof(float complex));
    for (size_t i = 0; i < burst_length - sps / 2; i++) {
        carrier_sync_out_qpsk[i] = crealf(carrier_sync_out[i]) +
                                   I * cimagf(carrier_sync_out[i + sps / 2]);
    }
    for (size_t i = burst_length - sps / 2; i < burst_length; i++) {
        carrier_sync_out_qpsk[i] = 0.0f;
    }

    const int preamble_offset_chips = 5000;  // MATLAB-compliant offset (2500 QPSK symbols)
    const int preamble_offset = preamble_offset_chips / 2;

    float complex *preamble_full = malloc((num_preamble_chips / 2 - preamble_offset) * sizeof(float complex));
    memcpy(preamble_full,
           preamble_qpsk + preamble_offset,
           (num_preamble_chips / 2 - preamble_offset) * sizeof(float complex));

    float complex *corr_buffer2 = malloc(burst_length * sizeof(float complex));
    int fsp_samp_idx = polyphase_correlator(
        carrier_sync_out_qpsk, burst_length,
        preamble_full, num_preamble_chips / 2 - preamble_offset,
        sps, corr_buffer2, preamble_offset);

    if (fsp_samp_idx < 0) {
        printf("ERROR: Preamble lost after frequency correction\n");
        // Cleanup
        free(rx_burst);
        free(coarse_sync_out);
        free(carrier_sync_out);
        free(phase_err);
        free(carrier_sync_out_qpsk);
        free(preamble_full);
        free(corr_buffer2);
        free(sample_buffer);
        free(preamble_qpsk);
        free(preamble_detect);
        free(prn_i);
        free(prn_q);
        free(corr_buffer);
        return -2;
    }
    free(preamble_full);
    free(corr_buffer2);

    // Step 3 - Timing Recovery (V11.0 MATLAB-compliant)
    printf("\n=== V11.0 Timing Recovery (Zero-Crossing TED) ===\n");

    timing_recovery_state_t symbol_synchronizer;
    int ret = timing_recovery_init(&symbol_synchronizer, sps, 0.001f, 2.0f, 4.0f);
    if (ret != 0) {
        printf("ERROR: Timing recovery initialization failed\n");
        // Cleanup
        free(rx_burst);
        free(coarse_sync_out);
        free(carrier_sync_out);
        free(phase_err);
        free(carrier_sync_out_qpsk);
        free(sample_buffer);
        free(preamble_qpsk);
        free(preamble_detect);
        free(prn_i);
        free(prn_q);
        free(corr_buffer);
        return -3;
    }

    float complex *synced_qpsk = malloc(DSSS_PACKET_CHIPS * 2 * sizeof(float complex));
    size_t num_symbols = 0;

    // Process timing recovery from OQPSK signal (NOT QPSK-converted)
    // The timing recovery handles OQPSK internally with Q-delay buffer
    ret = timing_recovery_process(&symbol_synchronizer,
                                  carrier_sync_out + fsp_samp_idx,
                                  burst_length - fsp_samp_idx,
                                  synced_qpsk,
                                  &num_symbols);

    timing_recovery_free(&symbol_synchronizer);

    if (ret != 0 || num_symbols == 0) {
        printf("ERROR: Timing recovery processing failed\n");
        free(synced_qpsk);
        free(rx_burst);
        free(coarse_sync_out);
        free(carrier_sync_out);
        free(phase_err);
        free(carrier_sync_out_qpsk);
        free(sample_buffer);
        free(preamble_qpsk);
        free(preamble_detect);
        free(prn_i);
        free(prn_q);
        free(corr_buffer);
        return -3;
    }

    printf("Timing recovery complete: %zu symbols recovered (expected ~%d)\n",
           num_symbols, DSSS_PACKET_CHIPS);

    // Step 4 - Demodulation and Phase Ambiguity Resolution
    printf("\n=== Phase Ambiguity Resolution ===\n");

    // Form chip sequence of preamble for phase ambiguity resolution
    int8_t *preamble_chips = malloc(6400);
    for (int i = 0; i < 3200; i++) {
        preamble_chips[2*i] = prn_i[i];
        preamble_chips[2*i + 1] = prn_q[i];
    }

    // Test all phase rotations and I/Q orderings
    int p_best = 0, k_best = 0;
    int best_matches = 0;
    float best_correlation = 0.0f;

    for (int p = 0; p <= 1; p++) {
        for (int k = 0; k < 4; k++) {
            // Demodulate with rotation
            uint8_t *rx_sig = malloc(num_symbols * 2);
            qpsk_demod(synced_qpsk, num_symbols, rx_sig, k);

            // Extract I & Q bit streams
            uint8_t *rx_ci = malloc(num_symbols + 1);
            uint8_t *rx_cq = malloc(num_symbols + 1);

            for (size_t i = 0; i < num_symbols; i++) {
                rx_ci[i] = rx_sig[2*i];
                rx_cq[i] = rx_sig[2*i + 1];
            }
            rx_ci[num_symbols] = 0;
            rx_cq[num_symbols] = 0;

            // Test preamble match
            int matches = 0;
            size_t compare_len = (num_symbols < 3300) ? num_symbols : 3300;

            for (size_t i = 0; i < compare_len; i++) {
                int8_t test_i = (rx_ci[i + p] > 0) ? 1 : 0;
                int8_t test_q = (rx_cq[i] > 0) ? 1 : 0;
                int8_t ref_i = (prn_i[i] > 0) ? 1 : 0;
                int8_t ref_q = (prn_q[i] > 0) ? 1 : 0;

                if (test_i == ref_i) matches++;
                if (test_q == ref_q) matches++;
            }

            float correlation = (float)matches / (compare_len * 2);

            if (correlation > best_correlation) {
                best_correlation = correlation;
                best_matches = matches;
                p_best = p;
                k_best = k;
            }

            printf("[PHASE] Testing phase=%d, offset=%d: correlation=%.3f (%d/%zu)\n",
                   k, p, correlation, matches, compare_len * 2);

            free(rx_sig);
            free(rx_ci);
            free(rx_cq);

            if (best_matches > compare_len * 1.8) {
                goto phase_found;
            }
        }
    }

phase_found:
    printf("Best phase: rotation=%d, offset=%d, matches=%d (%.1f%%)\n",
           k_best, p_best, best_matches, best_correlation * 100.0f);

    free(preamble_chips);

    // Final demodulation with correct phase
    uint8_t *rx_sig = malloc(num_symbols * 2);
    qpsk_demod(synced_qpsk, num_symbols, rx_sig, k_best);

    // Extract I and Q chip streams
    int8_t *rx_ci = malloc(DSSS_PACKET_CHIPS);
    int8_t *rx_cq = malloc(DSSS_PACKET_CHIPS);

    for (size_t i = 0; i < DSSS_PACKET_CHIPS && (i + p_best) < num_symbols; i++) {
        rx_ci[i] = rx_sig[2 * (i + p_best)];
        rx_cq[i] = rx_sig[2 * (i + p_best) + 1];
    }

    // Step 5 - DSSS Despreading
    printf("\n=== DSSS Despreading ===\n");

    // Convert to bipolar
    int8_t *rx_ci_bipolar = malloc(DSSS_PACKET_CHIPS);
    int8_t *rx_cq_bipolar = malloc(DSSS_PACKET_CHIPS);
    for (size_t i = 0; i < DSSS_PACKET_CHIPS; i++) {
        rx_ci_bipolar[i] = (rx_ci[i] == 0) ? -1 : +1;
        rx_cq_bipolar[i] = (rx_cq[i] == 0) ? -1 : +1;
    }

    // Correlate with PRN sequences
    uint8_t *ibits = malloc(DSSS_BITS_PER_CHANNEL);
    uint8_t *qbits = malloc(DSSS_BITS_PER_CHANNEL);

    for (int bit_idx = 0; bit_idx < DSSS_BITS_PER_CHANNEL; bit_idx++) {
        int i_sum = 0;
        int q_sum = 0;

        for (int chip = 0; chip < DSSS_SPREADING_FACTOR; chip++) {
            int chip_idx = bit_idx * DSSS_SPREADING_FACTOR + chip;
            i_sum += rx_ci_bipolar[chip_idx] * prn_i[chip_idx];
            q_sum += rx_cq_bipolar[chip_idx] * prn_q[chip_idx];
        }

        ibits[bit_idx] = (i_sum < 0) ? 1 : 0;
        qbits[bit_idx] = (q_sum < 0) ? 1 : 0;
    }

    free(rx_ci_bipolar);
    free(rx_cq_bipolar);

    // Demultiplex I and Q streams into single bitstream
    uint8_t *despread_message = malloc(DSSS_TOTAL_BITS);
    for (int i = 0; i < DSSS_BITS_PER_CHANNEL; i++) {
        despread_message[2*i] = ibits[i];
        despread_message[2*i + 1] = qbits[i];
    }

    // Check preamble match (first 50 bits should be all zeros)
    int preamble_errors = 0;
    for (int i = 0; i < DSSS_PREAMBLE_LENGTH; i++) {
        if (despread_message[i] != 0) preamble_errors++;
    }
    printf("DSSS despreading complete, preamble_errors=%d/%d (%.1f%%)\n",
           preamble_errors, DSSS_PREAMBLE_LENGTH,
           100.0f * preamble_errors / DSSS_PREAMBLE_LENGTH);

    // Output 250 bits (50 preamble + 200 payload data)
    memcpy(output_bits, despread_message, 250);

    printf("Output: 250 raw bits ready for BCH decoding\n");

    // Cleanup all allocated memory
    free(sample_buffer);
    free(preamble_qpsk);
    free(preamble_detect);
    free(prn_i);
    free(prn_q);
    free(corr_buffer);
    free(rx_burst);
    free(coarse_sync_out);
    free(carrier_sync_out);
    free(phase_err);
    free(carrier_sync_out_qpsk);
    free(synced_qpsk);
    free(rx_sig);
    free(rx_ci);
    free(rx_cq);
    free(ibits);
    free(qbits);
    free(despread_message);

    printf("\n=== DSSS OQPSK Receiver V11.0 Complete ===\n\n");
    return 0;
}
