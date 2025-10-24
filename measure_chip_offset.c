/**
 * @file measure_chip_offset.c
 * @brief Diagnostic tool to measure optimal chip offset
 *
 * Tests chip offsets from -50 to +50 on preamble correlation
 * to identify any residual timing synchronization bias.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include "dsss_demod.h"
#include "prn_generator.h"

#define OFFSET_MIN -50
#define OFFSET_MAX 50

/**
 * @brief Convert OQPSK to QPSK by removing Q channel delay
 */
static void oqpsk_to_qpsk(const float complex *oqpsk, float complex *qpsk,
                          size_t num_samples, int sps) {
    int delay = sps / 2;
    for (size_t i = 0; i < num_samples - delay; i++) {
        qpsk[i] = crealf(oqpsk[i]) + I * cimagf(oqpsk[i + delay]);
    }
}

/**
 * @brief Despread preamble with specific chip offset
 *
 * Returns correlation percentage on 50-bit preamble
 */
static float test_chip_offset(const uint8_t *chips_i, const uint8_t *chips_q,
                               int chip_offset, int chip_conv, int prn_conv) {
    // Generate PRN for preamble (25 I bits + 25 Q bits = 50 OQPSK symbols)
    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    int8_t prn_i_full[25 * 256];
    int8_t prn_q_full[25 * 256];

    // Generate I-channel PRN
    int8_t prn_chunk[256];
    for (int bit = 0; bit < 25; bit++) {
        prn_generate_i(&prn_state, prn_chunk);
        for (int c = 0; c < 256; c++) {
            if (prn_conv == 0) {
                prn_i_full[bit * 256 + c] = (prn_chunk[c] == -1) ? 1 : 0;
            } else {
                prn_i_full[bit * 256 + c] = (prn_chunk[c] == -1) ? 0 : 1;
            }
        }
    }

    // Reset for Q channel
    prn_init(&prn_state, 0);

    // Generate Q-channel PRN
    for (int bit = 0; bit < 25; bit++) {
        prn_generate_q(&prn_state, prn_chunk);
        for (int c = 0; c < 256; c++) {
            if (prn_conv == 0) {
                prn_q_full[bit * 256 + c] = (prn_chunk[c] == -1) ? 1 : 0;
            } else {
                prn_q_full[bit * 256 + c] = (prn_chunk[c] == -1) ? 0 : 1;
            }
        }
    }

    // Despread preamble (25 I bits + 25 Q bits)
    int correct_bits = 0;

    for (int bit = 0; bit < 25; bit++) {
        int start_idx = bit * 256;

        // Despread I-channel
        int corr_i = 0;
        for (int c = 0; c < 256; c++) {
            int chip_idx = start_idx + c + chip_offset;
            if (chip_idx >= 0 && chip_idx < 38400) {
                uint8_t recv_chip = chip_conv ? (1 - chips_i[chip_idx]) : chips_i[chip_idx];
                if (recv_chip == prn_i_full[start_idx + c]) {
                    corr_i++;
                }
            }
        }

        // Despread Q-channel
        int corr_q = 0;
        for (int c = 0; c < 256; c++) {
            int chip_idx = start_idx + c + chip_offset;
            if (chip_idx >= 0 && chip_idx < 38400) {
                uint8_t recv_chip = chip_conv ? (1 - chips_q[chip_idx]) : chips_q[chip_idx];
                if (recv_chip == prn_q_full[start_idx + c]) {
                    corr_q++;
                }
            }
        }

        // Decode bits
        uint8_t bit_i = (corr_i > 128) ? 0 : 1;
        uint8_t bit_q = (corr_q > 128) ? 0 : 1;

        // Check against preamble pattern (alternating 0101...)
        if (bit_i == (bit * 2) % 2) correct_bits++;
        if (bit_q == (bit * 2 + 1) % 2) correct_bits++;
    }

    return (correct_bits * 100.0f) / 50.0f;
}

/**
 * @brief Main diagnostic
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.iq>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     CHIP OFFSET DIAGNOSTIC TOOL                           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Demodulate file to get chips
    uint8_t output_bits[300];
    dsss_demod_state_t state;
    memset(&state, 0, sizeof(state));

    printf("=== STEP 1: DEMODULATE IQ FILE ===\n");
    int ret = dsss_demodulate_file(filename, output_bits, &state);

    if (ret != 0) {
        fprintf(stderr, "Error: Demodulation failed (code %d)\n", ret);
        return 1;
    }

    printf("✓ Demodulation complete\n");
    printf("  Phase rotation: %d (%.0f°)\n", state.phase_rotation, state.phase_rotation * 90.0f);
    printf("  I/Q swap: %s\n", state.iq_swapped ? "Yes" : "No");
    printf("\n");

    // Reload file and extract chips manually
    printf("=== STEP 2: EXTRACT CHIPS FOR TESTING ===\n");

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    size_t num_samples = file_size / sizeof(float complex);
    printf("  File: %s\n", filename);
    printf("  Samples: %zu\n", num_samples);

    float complex *samples = malloc(num_samples * sizeof(float complex));
    if (!samples) {
        perror("malloc");
        fclose(fp);
        return 1;
    }

    fread(samples, sizeof(float complex), num_samples, fp);
    fclose(fp);

    // Apply AGC
    float complex *agc_out = malloc(num_samples * sizeof(float complex));
    float agc_gain;
    dsss_agc(samples, agc_out, num_samples, &agc_gain);
    free(samples);

    // Detect preamble
    int preamble_idx;
    float freq_offset, correlation;
    ret = dsss_detect_preamble(agc_out, num_samples, 400000, &preamble_idx,
                                &freq_offset, &correlation);

    if (ret != 0) {
        fprintf(stderr, "Error: Preamble not found\n");
        free(agc_out);
        return 1;
    }

    printf("  Preamble index: %d\n", preamble_idx);
    printf("  Frequency offset: %.1f Hz\n", freq_offset);
    printf("\n");

    // Apply frequency correction
    float complex *freq_corr_out = malloc(num_samples * sizeof(float complex));
    dsss_frequency_correction(agc_out, freq_corr_out, num_samples, freq_offset, 400000);
    free(agc_out);

    // Extract burst
    size_t burst_length = num_samples - preamble_idx;
    if (burst_length > 110000) burst_length = 110000;

    float complex *burst = freq_corr_out + preamble_idx;

    // Apply OQPSK to QPSK conversion
    int sps = 10;  // 400kHz / 38.4kchips/s
    float complex *qpsk_out = malloc(burst_length * sizeof(float complex));
    oqpsk_to_qpsk(burst, qpsk_out, burst_length, sps);
    size_t qpsk_length = burst_length - sps / 2;

    // Timing recovery
    float complex *symbols = malloc(40000 * sizeof(float complex));
    size_t num_symbols = 0;
    dsss_timing_recovery(qpsk_out, symbols, qpsk_length, &num_symbols, 400000);
    free(qpsk_out);
    free(freq_corr_out);

    printf("  Symbols recovered: %zu\n", num_symbols);
    printf("\n");

    // Apply phase rotation from state
    for (size_t i = 0; i < num_symbols; i++) {
        float complex s = symbols[i];
        for (int r = 0; r < state.phase_rotation; r++) {
            s *= -I;  // Rotate by 90°
        }
        if (state.iq_swapped) {
            s = cimagf(s) + I * crealf(s);
        }
        symbols[i] = s;
    }

    // Convert to chips
    uint8_t *chips_i = malloc(38400);
    uint8_t *chips_q = malloc(38400);

    for (size_t i = 0; i < 38400 && i < num_symbols; i++) {
        chips_i[i] = (crealf(symbols[i]) >= 0) ? 0 : 1;
        chips_q[i] = (cimagf(symbols[i]) >= 0) ? 0 : 1;
    }

    free(symbols);

    // Test chip offsets
    printf("=== STEP 3: SCAN CHIP OFFSETS (%d to %d) ===\n", OFFSET_MIN, OFFSET_MAX);
    printf("\n");
    printf("Offset    Corr%%      Bar Chart\n");
    printf("------    -------   ---------\n");

    float best_corr = 0;
    int best_offset = 0;

    for (int offset = OFFSET_MIN; offset <= OFFSET_MAX; offset++) {
        float corr = test_chip_offset(chips_i, chips_q, offset,
                                       state.chip_convention, state.prn_conversion);

        if (corr > best_corr) {
            best_corr = corr;
            best_offset = offset;
        }

        // Print bar chart
        printf("%+4d      %5.1f%%   ", offset, corr);
        int bars = (int)(corr / 2.0f);  // 50 chars max
        for (int i = 0; i < bars; i++) printf("█");
        if (corr > 90.0f) printf(" ✓ EXCELLENT");
        else if (corr > 70.0f) printf(" ✓");
        printf("\n");
    }

    printf("\n");
    printf("=== RESULTS ===\n");
    printf("Best offset: %+d chips\n", best_offset);
    printf("Best correlation: %.1f%%\n", best_corr);
    printf("\n");

    if (best_corr < 90.0f) {
        printf("⚠ Correlation < 90%% indicates structural problem beyond simple offset\n");
    } else {
        printf("✓ Good correlation achieved with offset = %d\n", best_offset);
        printf("  → Update chip_offset in dsss_resolve_phase_ambiguity()\n");
    }

    free(chips_i);
    free(chips_q);

    return 0;
}
