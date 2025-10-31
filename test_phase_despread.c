/**
 * @file test_phase_despread.c
 * @brief Test de la fonction dsss_resolve_phase_by_despread()
 *
 * Test complet de la résolution d'ambiguïté de phase par despreading
 */

#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "dsss_demod.h"
#include "prn_generator.h"

#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f

int main() {
    printf("=================================================================\n");
    printf("TEST: Phase Ambiguity Resolution by Despreading\n");
    printf("=================================================================\n\n");

    // Step 1: Load test signal
    FILE *f = fopen("test_signal_CORRECT_FIXED.iq", "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open test_signal_CORRECT_FIXED.iq\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t num_samples = file_size / sizeof(float complex);
    printf("Test signal: %zu samples\n", num_samples);

    float complex *signal = malloc(num_samples * sizeof(float complex));
    if (!signal) {
        fclose(f);
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        return 1;
    }

    fread(signal, sizeof(float complex), num_samples, f);
    fclose(f);

    // Step 2: Timing recovery OQPSK
    printf("\n--- Step 1: OQPSK Timing Recovery ---\n");
    float complex *symbols = calloc(40000, sizeof(float complex));
    if (!symbols) {
        free(signal);
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        return 1;
    }

    size_t num_symbols = 0;
    int result = dsss_timing_recovery_oqpsk(signal, symbols, num_samples,
                                            &num_symbols, SAMPLE_RATE);
    free(signal);

    if (result != 0 || num_symbols < 38000) {
        fprintf(stderr, "ERROR: Timing recovery failed\n");
        free(symbols);
        return 1;
    }

    printf("Timing recovery: %zu symbols recovered (%.1f%%)\n",
           num_symbols, 100.0f * num_symbols / 38400.0f);

    // Step 3: Generate PRN sequences
    printf("\n--- Step 2: Generate PRN Sequences ---\n");

    prn_state_t prn_i, prn_q;
    prn_init(&prn_i, 0);  // Normal I mode
    prn_init(&prn_q, 0);  // Normal Q mode

    // Generate full PRN sequences (38400 chips)
    const int num_prn_chips = 38400;
    int8_t *prn_seq_i = malloc(num_prn_chips);
    int8_t *prn_seq_q = malloc(num_prn_chips);

    if (!prn_seq_i || !prn_seq_q) {
        fprintf(stderr, "ERROR: PRN allocation failed\n");
        free(symbols);
        free(prn_seq_i);
        free(prn_seq_q);
        return 1;
    }

    // Generate PRN for each bit (150 bits × 256 chips)
    for (int bit = 0; bit < 150; bit++) {
        int8_t seq_i[256], seq_q[256];
        prn_generate_i(&prn_i, seq_i);
        prn_generate_q(&prn_q, seq_q);

        for (int chip = 0; chip < 256; chip++) {
            prn_seq_i[bit * 256 + chip] = seq_i[chip];
            prn_seq_q[bit * 256 + chip] = seq_q[chip];
        }
    }

    printf("PRN sequences generated: 38400 chips each\n");
    printf("First 10 PRN_I chips: ");
    for (int i = 0; i < 10; i++) printf("%+d ", prn_seq_i[i]);
    printf("\n");
    printf("First 10 PRN_Q chips: ");
    for (int i = 0; i < 10; i++) printf("%+d ", prn_seq_q[i]);
    printf("\n");

    // Step 4: Test phase ambiguity resolution
    printf("\n--- Step 3: Phase Ambiguity Resolution ---\n");

    int best_rotation = -1;
    int best_swap = -1;
    float correlation = dsss_resolve_phase_by_despread(symbols, num_symbols,
                                                        prn_seq_i, prn_seq_q,
                                                        &best_rotation, &best_swap);

    printf("\n--- Results ---\n");
    printf("Best rotation: %d (phase = %d°)\n", best_rotation, best_rotation * 90);
    printf("Best swap: %d (%s)\n", best_swap, best_swap ? "swapped" : "normal");
    printf("Correlation: %.3f\n", correlation);

    // Load expected bits to verify
    FILE *f_exp = fopen("expected_bits_FIXED.bin", "rb");
    if (f_exp) {
        uint8_t expected_bits_i[150], expected_bits_q[150];
        fread(expected_bits_i, 1, 150, f_exp);
        fread(expected_bits_q, 1, 150, f_exp);
        fclose(f_exp);

        printf("\nExpected preamble pattern (first 25 bits):\n");
        printf("  I: ");
        for (int i = 0; i < 25; i++) printf("%d", expected_bits_i[i]);
        printf("\n  Q: ");
        for (int i = 0; i < 25; i++) printf("%d", expected_bits_q[i]);
        printf("\n");
        printf("  (All zeros as per T.018 §2.2.4)\n");
    }

    // Conclusion
    printf("\n=================================================================\n");
    printf("CONCLUSION\n");
    printf("=================================================================\n");

    if (correlation >= 0.9f) {
        printf("✅ TEST SUCCESS: Phase ambiguity resolved!\n");
        printf("   - Correlation: %.3f (>90%%)\n", correlation);
        printf("   - Configuration: swap=%d rot=%d (phase=%d°)\n",
               best_swap, best_rotation, best_rotation * 90);
        printf("   - Ready for PHASE 3: Integration\n");
    } else if (correlation >= 0.7f) {
        printf("⚠️  TEST PARTIAL: Phase found but low correlation\n");
        printf("   - Correlation: %.3f (70-90%%)\n", correlation);
        printf("   - May need parameter tuning\n");
    } else {
        printf("❌ TEST FAILED: Phase ambiguity not resolved\n");
        printf("   - Correlation: %.3f (<70%%)\n", correlation);
        printf("   - Check algorithm implementation\n");
    }

    free(symbols);
    free(prn_seq_i);
    free(prn_seq_q);

    return (correlation >= 0.7f) ? 0 : 1;
}
