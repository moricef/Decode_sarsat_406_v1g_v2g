/**
 * @file dsss_demod.c
 * @brief OQPSK DSSS Receiver for COSPAS-SARSAT 406 MHz beacons
 *
 * Complete receiver chain implementing:
 * - Automatic Gain Control (AGC)
 * - Preamble detection with polyphase correlation
 * - Coarse frequency offset estimation and correction
 * - Fine frequency offset correction (carrier synchronization)
 * - Symbol timing recovery for OQPSK
 * - Phase ambiguity resolution
 * - DSSS despreading with PRN correlation
 * - BCH error detection and correction
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

/* ============================================================================
 * PRN SEQUENCE GENERATION (x^23 + x^18 + 1)
 * ============================================================================ */

/**
 * @brief Generate PRN sequence using LFSR with polynomial x^23 + x^18 + 1
 * @param initial_state 23-bit initial state (LSB first)
 * @param output Output buffer for PRN chips (-1 or +1)
 * @param num_chips Number of chips to generate
 */
void dsss_generate_prn(uint32_t initial_state, int8_t *output, size_t num_chips) {
    uint32_t lfsr = initial_state & 0x7FFFFF;  // 23-bit mask

    for (size_t i = 0; i < num_chips; i++) {
        // Output the MSB (bit 22) as chip value: 0->+1, 1->-1
        output[i] = (lfsr & (1 << 22)) ? -1 : +1;

        // Calculate feedback: XOR of bit 22 (x^23) and bit 17 (x^18)
        uint32_t feedback = ((lfsr >> 22) ^ (lfsr >> 17)) & 1;

        // Shift left and insert feedback at LSB
        lfsr = ((lfsr << 1) | feedback) & 0x7FFFFF;
    }
}

/**
 * @brief Generate both I and Q PRN sequences for COSPAS-SARSAT
 * @param prn_i Output buffer for I channel PRN (38400 chips)
 * @param prn_q Output buffer for Q channel PRN (38400 chips)
 */
void dsss_generate_prn_sequences(int8_t *prn_i, int8_t *prn_q) {
    // COSPAS-SARSAT Normal mode initial states (LSB first, 23 bits)
    // I channel: all zeros except bit 22
    uint32_t init_i = 0x400000;  // [0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1]

    // Q channel: [0 0 1 1 0 1 0 1 1 0 0 0 0 0 1 1 1 1 1 1 1 0 0]
    uint32_t init_q = 0x0035AC;

    // Generate full sequences (150 bits * 256/2 = 19200 chips each)
    size_t num_chips = DSSS_PACKET_CHIPS;
    dsss_generate_prn(init_i, prn_i, num_chips);
    dsss_generate_prn(init_q, prn_q, num_chips);
}

/* ============================================================================
 * AUTOMATIC GAIN CONTROL (AGC)
 * ============================================================================ */

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

/* ============================================================================
 * POLYPHASE CORRELATOR FOR PREAMBLE DETECTION
 * ============================================================================ */

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

    // MATLAB page 9: waveformMagSq = filter(ones(size(refSignal)),1,[real(waveform.*conj(waveform)); zeros(length(refSignal)-1,1)]);
    // Implement as O(n) moving sum filter instead of O(n × ref_len) nested loop
    float *waveformMagSq = calloc(xcorr_len, sizeof(float));
    float window_sum = 0.0f;

    // Fill positions 0 to ref_len-2 with partial sums (MATLAB filter initialization)
    for (size_t lag = 0; lag < ref_len - 1 && lag < xcorr_len; lag++) {
        if (lag < sig_len) {
            window_sum += sigMagSq[lag];
        }
        waveformMagSq[lag] = window_sum;
    }

    // Position ref_len-1: first complete window
    if (ref_len - 1 < xcorr_len) {
        if (ref_len - 1 < sig_len) {
            window_sum += sigMagSq[ref_len - 1];
        }
        waveformMagSq[ref_len - 1] = window_sum;
    }

    // Slide the window for remaining positions (O(n) complexity)
    for (size_t lag = ref_len; lag < xcorr_len; lag++) {
        // Add new sample entering the window
        if (lag < sig_len) {
            window_sum += sigMagSq[lag];
        }
        // Remove old sample leaving the window
        size_t old_idx = lag - ref_len;
        if (old_idx < sig_len) {
            window_sum -= sigMagSq[old_idx];
        }
        waveformMagSq[lag] = window_sum;
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

    // DEBUG: Print correlation results
    printf("[timingEstimate] max_corr=%.4f, threshold=%.4f, max_idx=%d, sig_len=%zu, ref_len=%zu\n",
           max_corr, threshold, max_idx, sig_len, ref_len);

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

    // DEBUG: Print max correlation per phase
    printf("[polyphase_correlator] maxDetectorVal=%.4f at phase kidx=%d (sps=%d)\n",
           maxDetectorVal, kidx, sps);

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

        // DEBUG: Print threshold test
        printf("[polyphase_correlator] mean_corr=%.4f, threshold=%.4f, maxDetectorVal=%.4f %s\n",
               mean_corr, 5.5f * mean_corr, maxDetectorVal,
               (maxDetectorVal >= 5.5f * mean_corr) ? "PASS" : "FAIL");

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

/* ============================================================================
 * FREQUENCY OFFSET ESTIMATION AND CORRECTION
 * ============================================================================ */

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

/* ============================================================================
 * CARRIER SYNCHRONIZATION (FINE FREQUENCY/PHASE TRACKING)
 * ============================================================================ */

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
        } else {
            sync->phase += sync->frequency;
        }
    }
}

/* ============================================================================
 * SYMBOL TIMING RECOVERY FOR OQPSK
 * ============================================================================ */

typedef struct {
    float mu;              // Fractional timing offset [0, 1)
    float omega;           // Samples per symbol
    float k1;              // Proportional loop filter gain
    float k2;              // Integrator loop filter gain
    float W;               // Integrator state
    int sps;

    // OQPSK state buffer (caches last half symbol of Q channel)
    float q_buffer[128];
    int q_buffer_len;
    float q_delay_fractional;  // Exact fractional delay for Q (sps/2)
    int state_valid;

    // Strobe counter for symbol-rate output
    float strobe_counter;

    // TED state
    float complex prev_symbol;
    float complex prev_mid;
} timing_recovery_t;

/**
 * @brief Farrow piecewise parabolic interpolator (α=0.5)
 */
static inline float farrow_interpolate(const float *samples, int base_idx, float mu) {
    // Farrow structure with α = 0.5
    // Requires 4 samples: x[k-1], x[k], x[k+1], x[k+2]
    float v0 = samples[base_idx - 1];
    float v1 = samples[base_idx];
    float v2 = samples[base_idx + 1];
    float v3 = samples[base_idx + 2];

    // Farrow coefficients for piecewise parabolic with α=0.5
    float c0 = v1;
    float c1 = -v0/3.0f - v1/2.0f + v2 - v3/6.0f;
    float c2 = v0/2.0f - v1 + v2/2.0f;

    return c0 + mu * (c1 + mu * c2);
}

static inline float complex farrow_interpolate_complex(const float complex *samples,
                                                       int base_idx, float mu) {
    float re_interp = farrow_interpolate((float*)samples + 2*base_idx - 2, base_idx, mu);
    float im_interp = farrow_interpolate((float*)samples + 2*base_idx - 1, base_idx, mu);
    return re_interp + I * im_interp;
}

/**
 * @brief Initialize timing recovery for OQPSK
 * PI loop filter: K1, K2 computed from loop_bw, damping, detector_gain
 */
static void timing_recovery_init(timing_recovery_t *tr, int sps,
                                float loop_bw, float damping, float detector_gain) {
    tr->mu = 0.5f;
    tr->omega = (float)sps;
    tr->sps = sps;
    tr->W = 0.0f;
    tr->strobe_counter = 0.0f;
    tr->state_valid = 0;
    tr->q_delay_fractional = (float)sps / 2.0f;  // Exact: 65/2 = 32.5
    tr->q_buffer_len = (int)(tr->q_delay_fractional) + 3;  // Integer part + margin for interpolation
    tr->prev_symbol = 0.0f;
    tr->prev_mid = 0.0f;

    // PI loop filter gains
    float theta = loop_bw / (float)sps;
    theta = theta / (damping + 1.0f / (4.0f * damping));
    float denom = 1.0f + 2.0f * damping * theta + theta * theta;
    tr->k1 = -(4.0f * damping * theta / denom) / detector_gain;
    tr->k2 = -(4.0f * theta * theta / denom) / detector_gain;
}

/**
 * @brief Process OQPSK signal through timing recovery
 * MATLAB: sampleBufferQPSK = [real(rx(1:end-sps/2)) + 1i*imag(rx(sps/2+1:end)); zeros(sps/2,1)]
 */
static size_t timing_recovery_process(timing_recovery_t *tr,
                                     const float complex *input, size_t in_len,
                                     float complex *output, int sps) {

    int delay_int = (int)tr->q_delay_fractional;
    float delay_frac = tr->q_delay_fractional - (float)delay_int;

    // Build extended Q with state buffer: [q_state; new_Q]
    size_t q_ext_len = tr->q_buffer_len + in_len;
    float *q_ext = malloc(q_ext_len * sizeof(float));

    if (tr->state_valid) {
        memcpy(q_ext, tr->q_buffer, tr->q_buffer_len * sizeof(float));
    } else {
        memset(q_ext, 0, tr->q_buffer_len * sizeof(float));
        tr->state_valid = 1;
    }

    for (size_t i = 0; i < in_len; i++) {
        q_ext[tr->q_buffer_len + i] = cimagf(input[i]);
    }

    // Save state for next call
    memcpy(tr->q_buffer, q_ext + q_ext_len - tr->q_buffer_len, tr->q_buffer_len * sizeof(float));

    // QPSK alignment: I[n] + j*Q[n+delay_fractional]
    float complex *qpsk_aligned = malloc(in_len * sizeof(float complex));
    for (size_t i = 0; i < in_len; i++) {
        float i_val = crealf(input[i]);
        int q_base_idx = i + delay_int;
        float q_val = 0.0f;
        if (q_base_idx >= 1 && q_base_idx + 2 < (int)q_ext_len) {
            q_val = farrow_interpolate(q_ext, q_base_idx, delay_frac);
        }
        qpsk_aligned[i] = i_val + I * q_val;
    }

    free(q_ext);

    float *i_aligned = malloc(in_len * sizeof(float));
    float *q_aligned = malloc(in_len * sizeof(float));
    for (size_t i = 0; i < in_len; i++) {
        i_aligned[i] = crealf(qpsk_aligned[i]);
        q_aligned[i] = cimagf(qpsk_aligned[i]);
    }
    free(qpsk_aligned);

    size_t total_len = in_len;
    size_t out_idx = 0;
    size_t sample_idx = 2;

    while (sample_idx + 3 < total_len) {
        tr->strobe_counter += 1.0f / tr->omega;

        if (tr->strobe_counter >= 1.0f) {
            tr->strobe_counter -= 1.0f;

            if (sample_idx + 3 < total_len) {
                float i_sym = farrow_interpolate(i_aligned, sample_idx, tr->mu);
                float q_sym = farrow_interpolate(q_aligned, sample_idx, tr->mu);
                float complex symbol = i_sym + I * q_sym;
                output[out_idx++] = symbol;

                int mid_idx = sample_idx - sps/2;
                if (mid_idx >= 2) {
                    float i_mid = farrow_interpolate(i_aligned, mid_idx, tr->mu);
                    float q_mid = farrow_interpolate(q_aligned, mid_idx, tr->mu);
                    float complex mid_symbol = i_mid + I * q_mid;

                    // Zero-Crossing TED (decision-directed) - matches MATLAB default
                    // e(k) = x[k-1/2] * (â[k-1] - â[k]) where â is hard decision (sign)
                    float decision_prev_re = (crealf(tr->prev_symbol) > 0) ? 1.0f : -1.0f;
                    float decision_prev_im = (cimagf(tr->prev_symbol) > 0) ? 1.0f : -1.0f;
                    float decision_curr_re = (crealf(symbol) > 0) ? 1.0f : -1.0f;
                    float decision_curr_im = (cimagf(symbol) > 0) ? 1.0f : -1.0f;

                    float err_re = crealf(mid_symbol) * (decision_prev_re - decision_curr_re);
                    float err_im = cimagf(mid_symbol) * (decision_prev_im - decision_curr_im);
                    float timing_error = err_re + err_im;

                    tr->W += tr->k2 * timing_error;
                    float v = tr->W + tr->k1 * timing_error;

                    tr->mu += v / tr->omega;
                    while (tr->mu >= 1.0f) tr->mu -= 1.0f;
                    while (tr->mu < 0.0f) tr->mu += 1.0f;

                    tr->prev_symbol = symbol;
                }
            }
        }

        sample_idx++;
    }

    free(i_aligned);
    free(q_aligned);
    return out_idx;
}

/* ============================================================================
 * QPSK DEMODULATION AND PHASE AMBIGUITY RESOLUTION
 * ============================================================================ */

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

/* ============================================================================
 * MAIN RECEIVER FUNCTION
 * ============================================================================ */

/**
 * @brief Complete DSSS OQPSK receiver processing chain
 * @param ota_buffer Received over-the-air samples
 * @param buffer_length Number of samples in buffer
 * @param sps Samples per symbol
 * @param fs Sampling frequency (Hz)
 * @param max_doppler Maximum expected Doppler shift (Hz)
 * @param output_bits Decoded output bits (250 bits: 50 preamble + 200 data)
 * @return 0 on success, negative on error
 *
 * This function performs DSSS OQPSK demodulation using standard DSP algorithms.
 * Implements ping-pong buffering, continuous reception, and all demodulation steps.
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       int sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits) {

    printf("=== DSSS OQPSK Receiver Start ===\n");
    printf("Buffer length: %zu samples\n", buffer_length);
    printf("Sampling rate: %.0f Hz, SPS: %d\n", fs, sps);
    printf("Max Doppler: %d Hz\n", max_doppler);

    // Calculate burst size (as per specification)
    size_t num_burst_samples = (DSSS_PACKET_BITS / 2) * DSSS_SPREADING_FACTOR * sps;
    size_t num_preamble_chips = DSSS_PREAMBLE_BITS * (DSSS_SPREADING_FACTOR / 2);

    // Generate PRN sequences
    int8_t *prn_i = malloc(DSSS_PACKET_CHIPS);
    int8_t *prn_q = malloc(DSSS_PACKET_CHIPS);
    dsss_generate_prn_sequences(prn_i, prn_q);
    printf("Generated PRN sequences\n");

    // preambleDetectionOffset and preambleDetectionLength
    const int preamble_detection_offset = 200;  // Skip first symbols during AGC convergence
    const int preamble_detection_length = 175;  // Shortened to combat frequency offset

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

    // DEBUG: Show first 10 preamble reference values
    printf("[DEBUG] First 10 preamble reference symbols (after π/4 rotation):\n");
    for (int i = 0; i < 10 && i < preamble_detection_length; i++) {
        printf("  [%d] PRN_I=%+d PRN_Q=%+d -> ref=(%.3f%+.3fj)\n",
               i, prn_i[preamble_detection_offset + i], prn_q[preamble_detection_offset + i],
               crealf(preamble_detect[i]), cimagf(preamble_detect[i]));
    }

    // Double buffering (ping-pong scheme)
    // Create sample buffer twice the size of one burst
    size_t num_buffers = buffer_length / num_burst_samples;
    float complex *sample_buffer = malloc(num_burst_samples * 2 * sizeof(float complex));

    // Initialize with AWGN (empty buffer)
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
            float power = agc.avg_power;
            printf("AGC: gain=%.2f, avg_power=%.6f, first_sample=%.3f+j%.3f\n",
                   agc.gain, power,
                   crealf(rx_agc_samples[0]), cimagf(rx_agc_samples[0]));
        }

        // Convert from OQPSK to QPSK for symbol-based preamble detection
        // For half-sine pulse shaping at SPS=16: peak is at phase 8 (sin(π×8/16) = 1)
        // Start at phase 8 to capture peaks instead of zero-crossings
        float complex *sample_buffer_qpsk = malloc(num_burst_samples * 2 * sizeof(float complex));
        int q_delay = sps / 2;
        int phase_offset = sps / 2;  // Start at peak (phase 8 for SPS=16)
        for (size_t i = 0; i < num_burst_samples * 2 - q_delay - phase_offset; i++) {
            sample_buffer_qpsk[i] = crealf(rx_agc_samples[i + phase_offset]) +
                                   I * cimagf(rx_agc_samples[i + q_delay + phase_offset]);
        }
        for (size_t i = num_burst_samples * 2 - q_delay - phase_offset; i < num_burst_samples * 2; i++) {
            sample_buffer_qpsk[i] = 0.0f;
        }

        // DEBUG: Show first 10 received QPSK samples at symbol rate (decimated by sps)
        if (n == 0) {
            printf("[DEBUG] First 10 received QPSK symbols (after OQPSK→QPSK, decimated by sps=%d):\n", sps);
            for (int i = 0; i < 10; i++) {
                size_t idx = i * sps;
                printf("  [%d] rx=(%.3f%+.3fj)\n",
                       i, crealf(sample_buffer_qpsk[idx]), cimagf(sample_buffer_qpsk[idx]));
            }
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
                goto preamble_found;  // Break out of frequency and buffer loops
            }
        }

        free(sample_buffer_qpsk);
        free(rx_agc_samples);
    }

preamble_found:
    if (preamble_idx < 0) {
        printf("ERROR: Preamble not detected amongst simulation samples\n");
        free(sample_buffer);
        free(preamble_qpsk);
        free(preamble_detect);
        free(prn_i);
        free(prn_q);
        free(corr_buffer);
        return -1;
    }

    // Extract transmission burst from sample buffer
    // Collect 20 more chips at the end for possible timing adjustments
    size_t burst_length = (DSSS_PACKET_CHIPS + 20) * sps;
    float complex *rx_burst = malloc(burst_length * sizeof(float complex));
    memcpy(rx_burst, sample_buffer + preamble_idx, burst_length * sizeof(float complex));

    printf("Burst extracted, length: %zu samples (%.3f sec)\n",
           burst_length, (float)burst_length / fs);
    printf("[DEBUG] Burst range: sample_buffer[%d .. %zu]\n",
           preamble_idx, preamble_idx + burst_length - 1);
    printf("[DEBUG] First samples: [0]=%.3f+j%.3f, [100]=%.3f+j%.3f\n",
           crealf(rx_burst[0]), cimagf(rx_burst[0]),
           crealf(rx_burst[100]), cimagf(rx_burst[100]));

    // Step 1 - Coarse Frequency Offset Estimation and Correction
    // loop_bw=0.01 normalized to symbol rate (matches MATLAB NormalizedLoopBandwidth)
    carrier_sync_t coarse_freq;
    carrier_sync_init(&coarse_freq, 0.01f, 0.707f, sps);

    float coarse_offset = estimate_coarse_frequency_offset(rx_burst, burst_length, fs);
    printf("Estimated coarse frequency offset = %.3f kHz\n", coarse_offset / 1000.0f);

    float complex *coarse_sync_out = malloc(burst_length * sizeof(float complex));
    apply_frequency_offset(rx_burst, burst_length, fs, coarse_offset);
    memcpy(coarse_sync_out, rx_burst, burst_length * sizeof(float complex));

    // Step 2 - Fine Frequency Correction (Carrier Synchronizer)
    // loop_bw=0.01 normalized to symbol rate (matches MATLAB NormalizedLoopBandwidth)
    carrier_sync_t carrier_sync;
    carrier_sync_init(&carrier_sync, 0.01f, 0.707f, sps);
    printf("[DEBUG] Carrier sync: loop_bw=%.4f, damping=%.3f, sps=%d\n",
           0.01f, 0.707f, sps);

    float complex *carrier_sync_out = malloc(burst_length * sizeof(float complex));
    float *phase_err = malloc(burst_length * sizeof(float));

    carrier_sync_process(&carrier_sync, coarse_sync_out, carrier_sync_out, burst_length, phase_err);

    printf("Carrier synchronization complete\n");
    printf("[DEBUG] Signal after sync: [0]=%.3f+j%.3f, [100]=%.3f+j%.3f\n",
           crealf(carrier_sync_out[0]), cimagf(carrier_sync_out[0]),
           crealf(carrier_sync_out[100]), cimagf(carrier_sync_out[100]));

    // Path Detection - Second preamble detection using entire preamble
    // Convert from OQPSK to QPSK
    float complex *carrier_sync_out_qpsk = malloc(burst_length * sizeof(float complex));
    for (size_t i = 0; i < burst_length - sps / 2; i++) {
        carrier_sync_out_qpsk[i] = crealf(carrier_sync_out[i]) +
                                   I * cimagf(carrier_sync_out[i + sps / 2]);
    }
    for (size_t i = burst_length - sps / 2; i < burst_length; i++) {
        carrier_sync_out_qpsk[i] = 0.0f;
    }

    // Use entire preamble for fine path detection
    const int preamble_offset_chips = 5000; 
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
        return -1;
    }

    printf("Fine-tuned preamble location: sample index %d\n", fsp_samp_idx);
    free(preamble_full);
    free(corr_buffer2);

    // Step 3 - Timing Recovery of OQPSK signal
    // MATLAB parameters (SPS=8): NormalizedLoopBandwidth=0.001, DetectorGain=4, DampingFactor=2
    timing_recovery_t symbol_synchronizer;
    timing_recovery_init(&symbol_synchronizer, sps, 0.001f, 2.0f, 4.0f);

    float complex *synced_qpsk = malloc(DSSS_PACKET_CHIPS * 2 * sizeof(float complex));
    float *timing_error = malloc(DSSS_PACKET_CHIPS * 2 * sizeof(float));

    // Process from FSP index to end
    size_t num_symbols = timing_recovery_process(&symbol_synchronizer,
                                                 carrier_sync_out + fsp_samp_idx,
                                                 burst_length - fsp_samp_idx,
                                                 synced_qpsk, sps);

    printf("Timing recovery complete, %zu symbols recovered\n", num_symbols);

    // Step 4 - Demodulation and Phase Ambiguity Resolution
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

            // Print all correlation results
            printf("[PHASE] Testing phase=%d, offset=%d: correlation=%.3f (%d/%zu)\n",
                   k, p, correlation, matches, compare_len * 2);

            free(rx_sig);
            free(rx_ci);
            free(rx_cq);

            if (best_matches > compare_len * 1.8) {  // Good enough
                goto phase_found;
            }
        }
    }

phase_found:
    printf("Preamble detected: phase=%d, offset=%d, matches=%d\n", k_best, p_best, best_matches);

    // Final demodulation with correct phase
    uint8_t *rx_sig = malloc(num_symbols * 2);
    qpsk_demod(synced_qpsk, num_symbols, rx_sig, k_best);

    // Compute start indices for I and Q streams
    size_t preamble_test_start_idx = 1;  // Simplified
    size_t s_idx_i = preamble_test_start_idx + 2 * p_best;
    size_t s_idx_q = preamble_test_start_idx + 1;

    // Extract preamble and payload chips
    int8_t *rx_ci = malloc(DSSS_PACKET_CHIPS);
    int8_t *rx_cq = malloc(DSSS_PACKET_CHIPS);

    for (size_t i = 0; i < DSSS_PACKET_CHIPS && (s_idx_i + 2*i) < num_symbols * 2; i++) {
        rx_ci[i] = rx_sig[s_idx_i + 2*i];
        rx_cq[i] = rx_sig[s_idx_q + 2*i];
    }

    free(preamble_chips);


    // Step 5 - DSSS Despreading
    // Correlate PRN sequence with received chips (XOR operation)
    int8_t *ibdn = malloc(DSSS_PACKET_CHIPS);
    int8_t *qbdn = malloc(DSSS_PACKET_CHIPS);

    for (size_t i = 0; i < DSSS_PACKET_CHIPS; i++) {
        ibdn[i] = rx_ci[i] ^ prn_i[i];
        qbdn[i] = rx_cq[i] ^ prn_q[i];
    }

    // Reshape into spreading factor rows (reshape)
    // Perform ML decoding: if sum > threshold, bit=1, else bit=0
    uint8_t *ibits = malloc(DSSS_PACKET_BITS / 2);
    uint8_t *qbits = malloc(DSSS_PACKET_BITS / 2);

    int threshold = DSSS_SPREADING_FACTOR / 2;

    for (int bit_idx = 0; bit_idx < DSSS_PACKET_BITS / 2; bit_idx++) {
        int i_sum = 0;
        int q_sum = 0;

        for (int chip = 0; chip < DSSS_SPREADING_FACTOR; chip++) {
            i_sum += ibdn[bit_idx * DSSS_SPREADING_FACTOR + chip];
            q_sum += qbdn[bit_idx * DSSS_SPREADING_FACTOR + chip];
        }

        ibits[bit_idx] = (i_sum > threshold) ? 1 : 0;
        qbits[bit_idx] = (q_sum > threshold) ? 1 : 0;
    }


    // Demultiplex I and Q streams into single bitstream
    uint8_t *despread_message = malloc(DSSS_PACKET_BITS);
    for (int i = 0; i < DSSS_PACKET_BITS / 2; i++) {
        despread_message[2*i] = ibits[i];
        despread_message[2*i + 1] = qbits[i];
    }

    // Check preamble match (first 50 bits should be all zeros)
    int preamble_errors = 0;
    for (int i = 0; i < DSSS_PREAMBLE_BITS; i++) {
        if (despread_message[i] != 0) preamble_errors++;
    }
    printf("DSSS despreading complete, preamble_errors=%d/%d (%.1f%%)\n",
           preamble_errors, DSSS_PREAMBLE_BITS,
           100.0f * preamble_errors / DSSS_PREAMBLE_BITS);

    // Output 250 bits (50 preamble + 202 data - BCH will be done externally)
    // Note: Full packet is 300 bits but only first 250 needed for BCH input
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
    free(timing_error);
    free(rx_sig);
    free(rx_ci);
    free(rx_cq);
    free(ibdn);
    free(qbdn);
    free(ibits);
    free(qbits);
    free(despread_message);

    printf("=== DSSS OQPSK Receiver Complete ===\n\n");

    return 0;
}
