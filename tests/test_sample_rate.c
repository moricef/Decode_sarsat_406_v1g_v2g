/**
 * @file test_sample_rate.c
 * @brief Standalone Sample Rate Estimation Tool with Preamble Correlation
 *
 * Tests multiple sample rate candidates by correlating with DSSS preamble pattern.
 * Uses sliding window correlation to find best match.
 *
 * Compile:
 *   gcc -o test_sample_rate test_sample_rate.c prn_generator.c -lm -O3
 *
 * Usage:
 *   ./test_sample_rate test_known.iq
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <stdint.h>
#include "prn_generator.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// COSPAS-SARSAT parameters
#define DSSS_CHIP_RATE          38400       // 38.4 kchips/s
#define DSSS_SPREADING_FACTOR   256         // 256 chips per bit
#define DSSS_PREAMBLE_LENGTH    50          // 50 bits alternating 0,1,0,1...

// =============================================================================
// PREAMBLE GENERATION
// =============================================================================

/**
 * @brief Generate DSSS preamble reference at chip rate
 *
 * Preamble pattern: 50 bits alternating 0,1,0,1,...
 * After DSSS spreading: 50 × 256 = 12,800 chips
 *
 * @param preamble_chips Output buffer (12,800 complex chips at chip rate)
 * @return 0 on success
 */
int generate_preamble_chips(float complex *preamble_chips) {
    prn_state_t prn_state_i, prn_state_q;
    prn_init(&prn_state_i, 0);  // Normal mode I-channel
    prn_init(&prn_state_q, 0);  // Normal mode Q-channel

    int8_t prn_i_buf[DSSS_SPREADING_FACTOR];
    int8_t prn_q_buf[DSSS_SPREADING_FACTOR];

    int chip_idx = 0;

    // Generate 25 bits per channel (I and Q interleaved = 50 bits total)
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
            // Convert PRN from {-1,+1} to chips considering bit value
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
 * @param preamble_chips Input chips at chip rate (12,800)
 * @param num_chips Number of chips (12,800)
 * @param preamble_samples Output upsampled preamble
 * @param sps Samples per chip (e.g., 10.4 for 400 kHz)
 * @return Number of output samples
 */
int upsample_preamble(const float complex *preamble_chips, int num_chips,
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
 * @brief Apply OQPSK Tc/2 delay to preamble (Q channel delayed by half chip)
 *
 * @param preamble Input QPSK preamble
 * @param output Output OQPSK preamble
 * @param num_samples Number of samples
 * @param sps Samples per chip
 */
void apply_oqpsk_delay(const float complex *preamble, float complex *output,
                       int num_samples, float sps) {
    int delay = (int)(sps / 2.0f);

    for (int i = 0; i < num_samples; i++) {
        float i_val = crealf(preamble[i]);
        float q_val = (i >= delay) ? cimagf(preamble[i - delay]) : 0.0f;
        output[i] = i_val + I * q_val;
    }
}

// =============================================================================
// CORRELATION FUNCTIONS
// =============================================================================

/**
 * @brief Compute normalized cross-correlation between two complex signals
 *
 * @param signal1 First signal
 * @param signal2 Second signal
 * @param len Length of both signals
 * @return Normalized correlation [0.0, 1.0]
 */
float correlate_complex(const float complex *signal1, const float complex *signal2, int len) {
    float complex corr_sum = 0.0f;
    float power1 = 0.0f, power2 = 0.0f;

    for (int i = 0; i < len; i++) {
        corr_sum += signal1[i] * conjf(signal2[i]);
        power1 += cabsf(signal1[i]) * cabsf(signal1[i]);
        power2 += cabsf(signal2[i]) * cabsf(signal2[i]);
    }

    // Normalized correlation
    if (power1 > 0 && power2 > 0) {
        return cabsf(corr_sum) / sqrtf(power1 * power2);
    }
    return 0.0f;
}

/**
 * @brief Sliding window preamble correlation
 *
 * Tests correlation at every position in the signal (sliding window).
 *
 * @param signal Input IQ signal
 * @param signal_len Signal length
 * @param preamble Reference preamble
 * @param preamble_len Preamble length
 * @param max_corr Output: maximum correlation found
 * @param max_idx Output: index where maximum occurs
 * @return 0 on success
 */
int sliding_correlation(const float complex *signal, int signal_len,
                        const float complex *preamble, int preamble_len,
                        float *max_corr, int *max_idx) {
    *max_corr = 0.0f;
    *max_idx = -1;

    // Limit search to first 50% of signal (preamble should be near start)
    int search_len = signal_len / 2;
    if (search_len < preamble_len) {
        fprintf(stderr, "Error: Signal too short for correlation\n");
        return -1;
    }

    // Step size for sliding window (trade-off speed vs precision)
    // Step by 1 chip worth of samples for good precision
    float sps = (float)preamble_len / (DSSS_PREAMBLE_LENGTH / 2 * DSSS_SPREADING_FACTOR);
    int step = (int)sps;
    if (step < 1) step = 1;

    printf("  Sliding correlation: search_len=%d, preamble_len=%d, step=%d\n",
           search_len, preamble_len, step);

    // Progress indicator
    int last_pct = -1;

    // Slide preamble across signal
    for (int offset = 0; offset <= search_len - preamble_len; offset += step) {
        float corr = correlate_complex(&signal[offset], preamble, preamble_len);

        if (corr > *max_corr) {
            *max_corr = corr;
            *max_idx = offset;
        }

        // Progress bar
        int pct = (offset * 100) / (search_len - preamble_len);
        if (pct != last_pct && pct % 10 == 0) {
            printf("    Progress: %d%%\r", pct);
            fflush(stdout);
            last_pct = pct;
        }
    }
    printf("    Progress: 100%%\n");

    return 0;
}

// =============================================================================
// SAMPLE RATE ESTIMATION
// =============================================================================

/**
 * @brief Test single sample rate candidate
 *
 * @param signal Input IQ signal
 * @param signal_len Signal length
 * @param samp_rate Sample rate candidate (Hz)
 * @param max_corr Output: maximum correlation
 * @param max_idx Output: index of maximum
 * @return 0 on success
 */
int test_sample_rate(const float complex *signal, int signal_len, float samp_rate,
                     float *max_corr, int *max_idx) {
    // Calculate samples per chip
    float sps = samp_rate / DSSS_CHIP_RATE;

    printf("\n[TEST] Sample rate: %.0f Hz (%.2f samples/chip)\n", samp_rate, sps);

    // Generate preamble at chip rate
    const int num_preamble_chips = (DSSS_PREAMBLE_LENGTH / 2) * DSSS_SPREADING_FACTOR;
    float complex *preamble_chips = malloc(num_preamble_chips * sizeof(float complex));
    if (!preamble_chips) return -1;

    generate_preamble_chips(preamble_chips);

    // Upsample to match signal sample rate
    int preamble_samples_len = (int)(num_preamble_chips * sps);
    float complex *preamble_qpsk = malloc(preamble_samples_len * sizeof(float complex));
    if (!preamble_qpsk) {
        free(preamble_chips);
        return -1;
    }

    upsample_preamble(preamble_chips, num_preamble_chips, preamble_qpsk, sps);

    // Apply OQPSK Tc/2 delay
    float complex *preamble_oqpsk = malloc(preamble_samples_len * sizeof(float complex));
    if (!preamble_oqpsk) {
        free(preamble_chips);
        free(preamble_qpsk);
        return -1;
    }

    apply_oqpsk_delay(preamble_qpsk, preamble_oqpsk, preamble_samples_len, sps);

    printf("  Preamble: %d chips → %d samples\n", num_preamble_chips, preamble_samples_len);

    // Sliding correlation
    int result = sliding_correlation(signal, signal_len, preamble_oqpsk,
                                     preamble_samples_len, max_corr, max_idx);

    printf("  Result: corr=%.4f at index=%d (%.3f sec)\n",
           *max_corr, *max_idx, *max_idx / samp_rate);

    free(preamble_chips);
    free(preamble_qpsk);
    free(preamble_oqpsk);

    return result;
}

/**
 * @brief Estimate sample rate by testing multiple candidates
 *
 * @param signal Input IQ signal
 * @param signal_len Signal length
 * @return Estimated sample rate (Hz)
 */
float estimate_sample_rate(const float complex *signal, int signal_len) {
    // Extended candidate list covering common SDR sample rates
    float candidates[] = {
        300000.0f,    // 7.8 sps/chip
        384000.0f,    // 10.0 sps/chip (common)
        400000.0f,    // 10.4 sps/chip (SARSAT_SGB default)
        768000.0f,    // 20.0 sps/chip
        1000000.0f,   // 26.0 sps/chip
        1536000.0f,   // 40.0 sps/chip
        2000000.0f,   // 52.1 sps/chip
        2500000.0f,   // 65.1 sps/chip (PlutoSDR common)
        3072000.0f,   // 80.0 sps/chip
        6144000.0f    // 160 sps/chip
    };
    int num_candidates = sizeof(candidates) / sizeof(candidates[0]);

    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("        SAMPLE RATE ESTIMATION WITH PREAMBLE CORRELATION\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Signal length: %d samples\n", signal_len);
    printf("Testing %d candidate sample rates...\n", num_candidates);

    float best_corr = 0.0f;
    float best_sr = candidates[0];
    int best_idx = -1;

    for (int i = 0; i < num_candidates; i++) {
        float sr = candidates[i];
        float max_corr;
        int max_idx;

        if (test_sample_rate(signal, signal_len, sr, &max_corr, &max_idx) == 0) {
            if (max_corr > best_corr) {
                best_corr = max_corr;
                best_sr = sr;
                best_idx = max_idx;
                printf("  ✓ NEW BEST!\n");
            }
        }
    }

    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("                      FINAL RESULT\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Estimated sample rate: %.0f Hz\n", best_sr);
    printf("Best correlation: %.4f\n", best_corr);
    printf("Preamble found at: index=%d (%.3f sec)\n", best_idx, best_idx / best_sr);
    printf("Samples per chip: %.2f\n", best_sr / DSSS_CHIP_RATE);

    if (best_corr < 0.3f) {
        printf("\n⚠️  WARNING: Low correlation (%.4f)\n", best_corr);
        printf("Possible causes:\n");
        printf("  - Sample rate not in candidate list\n");
        printf("  - Signal quality too low\n");
        printf("  - Preamble corrupted or missing\n");
        printf("  - Frequency offset too large\n");
    } else if (best_corr < 0.5f) {
        printf("\n⚠️  Moderate correlation (%.4f) - results may be uncertain\n", best_corr);
    } else {
        printf("\n✓ Good correlation (%.4f) - sample rate estimation reliable\n", best_corr);
    }

    printf("═══════════════════════════════════════════════════════════\n\n");

    return best_sr;
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <iq_file>\n", argv[0]);
        fprintf(stderr, "Example: %s test_known.iq\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    // Load IQ file
    printf("Loading IQ file: %s\n", filename);
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return 1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // IQ format: interleaved float32 (I, Q, I, Q, ...)
    int num_samples = file_size / (2 * sizeof(float));

    printf("File size: %ld bytes\n", file_size);
    printf("Samples: %d\n", num_samples);

    if (num_samples < 10000) {
        fprintf(stderr, "Error: File too small (need at least 10k samples)\n");
        fclose(fp);
        return 1;
    }

    // Limit to first 1M samples for speed
    if (num_samples > 1000000) {
        printf("Limiting to first 1M samples for speed\n");
        num_samples = 1000000;
    }

    // Allocate and load
    float complex *iq_samples = malloc(num_samples * sizeof(float complex));
    if (!iq_samples) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(fp);
        return 1;
    }

    size_t read = fread(iq_samples, sizeof(float complex), num_samples, fp);
    fclose(fp);

    if (read != num_samples) {
        fprintf(stderr, "Warning: Read %zu samples, expected %d\n", read, num_samples);
        num_samples = read;
    }

    printf("Loaded %d samples\n", num_samples);

    // Estimate sample rate
    float estimated_sr = estimate_sample_rate(iq_samples, num_samples);

    // Summary
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("File: %s\n", filename);
    printf("Estimated sample rate: %.0f Hz\n", estimated_sr);
    printf("Recommended for dec406_iq: -s %.0f\n", estimated_sr);
    printf("═══════════════════════════════════════════════════════════\n");

    free(iq_samples);
    return 0;
}
