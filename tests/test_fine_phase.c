/**
 * @file test_fine_phase.c
 * @brief Test fine phase rotations (1° steps) + I/Q inversions
 *
 * Tests 360 rotations × 2 swaps × 2 inversions = 1440 combinations
 * to find optimal phase correction beyond discrete 90° steps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include "dsss_demod.h"
#include "prn_generator.h"

/**
 * @brief Test single phase rotation + swap + invert combination
 */
static float test_combination(const float complex *symbols, size_t num_symbols,
                              float angle_deg, bool swap_iq, bool invert_bits) {
    // Expected preamble pattern: alternating 01010101...
    // 25 I bits + 25 Q bits = 50 OQPSK symbols

    float angle_rad = angle_deg * M_PI / 180.0f;
    float complex rotation = cexpf(I * angle_rad);

    int correct_bits = 0;

    // Test first 50 symbols (preamble)
    for (size_t i = 0; i < 50 && i < num_symbols; i++) {
        float complex sym = symbols[i] * rotation;

        if (swap_iq) {
            sym = cimagf(sym) + I * crealf(sym);
        }

        uint8_t bit_i = (crealf(sym) >= 0) ? 0 : 1;
        uint8_t bit_q = (cimagf(sym) >= 0) ? 0 : 1;

        if (invert_bits) {
            bit_i = !bit_i;
            bit_q = !bit_q;
        }

        // Preamble expected: I bits = [0,1,0,1,...], Q bits = [0,1,0,1,...]
        uint8_t expected_i = (i * 2) % 2;
        uint8_t expected_q = (i * 2 + 1) % 2;

        if (bit_i == expected_i) correct_bits++;
        if (bit_q == expected_q) correct_bits++;
    }

    return (correct_bits * 100.0f) / 100.0f;  // 50 symbols × 2 bits = 100 bits
}

/**
 * @brief Main diagnostic
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.iq>\n", argv[0]);
        return 1;
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   FINE PHASE ROTATION + I/Q INVERSION DIAGNOSTIC          ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Demodulate to get symbols
    uint8_t output_bits[300];
    dsss_demod_state_t state;
    memset(&state, 0, sizeof(state));

    printf("=== STEP 1: DEMODULATE TO SYMBOLS ===\n");

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size_t num_samples = file_size / sizeof(float complex);

    float complex *samples = malloc(num_samples * sizeof(float complex));
    fread(samples, sizeof(float complex), num_samples, fp);
    fclose(fp);

    // AGC
    float complex *agc_out = malloc(num_samples * sizeof(float complex));
    float agc_gain;
    dsss_agc(samples, agc_out, num_samples, &agc_gain);
    free(samples);

    // Detect preamble
    int preamble_idx;
    float freq_offset, correlation;
    int ret = dsss_detect_preamble(agc_out, num_samples, 400000, &preamble_idx,
                                     &freq_offset, &correlation);
    if (ret != 0) {
        fprintf(stderr, "Error: Preamble not found\n");
        free(agc_out);
        return 1;
    }

    printf("  Preamble at index %d, freq offset %.1f Hz\n", preamble_idx, freq_offset);

    // Frequency correction
    float complex *freq_corr_out = malloc(num_samples * sizeof(float complex));
    dsss_frequency_correction(agc_out, freq_corr_out, num_samples, freq_offset, 400000);
    free(agc_out);

    // Extract burst
    size_t burst_length = num_samples - preamble_idx;
    if (burst_length > 384000) burst_length = 384000;
    float complex *burst = freq_corr_out + preamble_idx;

    // OQPSK to QPSK conversion
    int sps = 10;
    float complex *qpsk_out = malloc(burst_length * sizeof(float complex));

    // Inline OQPSK to QPSK
    int delay = sps / 2;
    for (size_t i = 0; i < burst_length - delay; i++) {
        qpsk_out[i] = crealf(burst[i]) + I * cimagf(burst[i + delay]);
    }
    size_t qpsk_length = burst_length - delay;

    // Timing recovery
    float complex *symbols = malloc(40000 * sizeof(float complex));
    size_t num_symbols = 0;
    dsss_timing_recovery(qpsk_out, symbols, qpsk_length, &num_symbols, 400000);
    free(qpsk_out);
    free(freq_corr_out);

    printf("  Recovered %zu symbols\n", num_symbols);
    printf("\n");

    // Test all combinations
    printf("=== STEP 2: TEST 1440 COMBINATIONS ===\n");
    printf("  (360 angles × 2 swaps × 2 inversions)\n");
    printf("\n");

    float best_corr = 0.0f;
    float best_angle = 0.0f;
    bool best_swap = false;
    bool best_invert = false;

    int progress = 0;
    int total = 360 * 2 * 2;

    for (int angle = 0; angle < 360; angle++) {
        for (int swap = 0; swap < 2; swap++) {
            for (int invert = 0; invert < 2; invert++) {
                float corr = test_combination(symbols, num_symbols,
                                              (float)angle, swap, invert);

                if (corr > best_corr) {
                    best_corr = corr;
                    best_angle = (float)angle;
                    best_swap = swap;
                    best_invert = invert;
                }

                progress++;
                if (progress % 144 == 0) {  // Every 10%
                    printf("  Progress: %d%% (best so far: %.1f%%)\r",
                           (progress * 100) / total, best_corr);
                    fflush(stdout);
                }
            }
        }
    }

    printf("\n\n");
    printf("=== RESULTS ===\n");
    printf("Best correlation: %.1f%%\n", best_corr);
    printf("Optimal angle: %.1f°\n", best_angle);
    printf("I/Q swap: %s\n", best_swap ? "YES" : "NO");
    printf("Bit inversion: %s\n", best_invert ? "YES" : "NO");
    printf("\n");

    // Analysis
    if (best_corr > 90.0f) {
        printf("✓ EXCELLENT! Problem identified:\n");
        printf("  → Update dsss_resolve_phase_ambiguity():\n");
        printf("     - Use rotation = %.1f° (not discrete 90° steps)\n", best_angle);
        printf("     - Apply I/Q swap = %s\n", best_swap ? "true" : "false");
        printf("     - Apply bit inversion = %s\n", best_invert ? "true" : "false");
        printf("\n");
        printf("  Expected improvement: 70%% → %.1f%%\n", best_corr);
    } else if (best_corr > 70.0f) {
        printf("✓ IMPROVEMENT: %.1f%% > 70%% (current)\n", best_corr);
        printf("  → Apply these parameters and re-test\n");
        printf("  → May need Étape 3 (SNR filtering) for >90%%\n");
    } else {
        printf("⚠ NO IMPROVEMENT: Phase rotation not the issue\n");
        printf("  → Move to Étape 3 (SNR + lowpass filter)\n");
        printf("  → Current SNR is likely too low (2.5 dB)\n");
    }

    // Additional analysis: plot correlation vs angle
    printf("\n");
    printf("=== CORRELATION vs ANGLE (best swap/invert) ===\n");
    printf("Angle   Corr%%   Bar\n");
    printf("-----   -----   ---\n");

    for (int angle = 0; angle < 360; angle += 5) {
        float corr = test_combination(symbols, num_symbols,
                                      (float)angle, best_swap, best_invert);
        printf("%3d°    %5.1f%%  ", angle, corr);

        int bars = (int)(corr / 2.0f);
        for (int i = 0; i < bars; i++) printf("█");

        if (fabs(angle - best_angle) < 2.5f) printf(" ← BEST");
        printf("\n");
    }

    free(symbols);

    return 0;
}
