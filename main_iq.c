/**
 * @file main_iq.c
 * @brief Entry point for COSPAS-SARSAT 2G IQ file demodulation
 *
 * ⚠️  WARNING: THIS DEMODULATOR IS NOT FUNCTIONAL ⚠️
 *
 * Current Status: PAUSED - 55.3% bit accuracy (need >95%)
 * Date: 2025-10-19
 *
 * This program will compile and run, but produces incorrect results.
 * See ETAT_PAUSE_DEMODULATEUR.md for complete bug analysis.
 *
 * Complete receiver chain:
 * 1. Load IQ file (.iq, .cfile format)
 * 2. DSSS/OQPSK demodulation → 300 bits (BUGGY - see dsss_demod.c)
 * 3. Extract payload (skip 50-bit preamble)
 * 4. Pass 250 bits to existing decoder
 * 5. Display results and statistics
 *
 * Usage:
 *   ./dec406_iq <filename.iq>
 *   ./dec406_iq <filename.cfile>
 *
 * Known Issues:
 * - Timing recovery incorrect (33k vs 38.4k symbols)
 * - Phase ambiguity tested on wrong data (spread vs despread)
 * - DSSS despreading low correlation (0.053 vs >0.7)
 * - Costas loop disabled due to divergence
 *
 * For operational workflow, see README_PAUSE.md:
 * - TX Generator (SARSAT_SGB): ✅ WORKS
 * - RX Decoder (dec406_v2g.c): ✅ WORKS
 * - IQ Demodulator (this file): ❌ DOES NOT WORK
 *
 * @author Collaborative development (2025)
 * @license Creative Commons CC BY-NC-SA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "dsss_demod.h"
#include "prn_generator.h"

// Stub for decode_beacon (to be implemented)
void decode_beacon(const uint8_t *bits, int length) {
    printf("[INFO] decode_beacon() stub: would decode %d bits\n", length);
    // TODO: Implement full BCH decoder and message parser
}

// =============================================================================
// MAIN PROGRAM
// =============================================================================

/**
 * @brief Print usage information
 */
static void print_usage(const char *progname) {
    printf("COSPAS-SARSAT 2G IQ Demodulator\n");
    printf("================================\n\n");
    printf("Usage: %s <filename.iq|filename.cfile> [-s sample_rate] [-f freq_offset]\n\n", progname);
    printf("Options:\n");
    printf("  -s <rate>  Specify sample rate in Hz (skip auto-detection)\n");
    printf("  -f <freq>  Force frequency offset in Hz (skip search, use 0 for synthetic signals)\n\n");
    printf("Supported formats:\n");
    printf("  .iq     - Raw complex float32 (interleaved I/Q)\n");
    printf("  .cfile  - GNU Radio complex float format\n\n");
    printf("Examples:\n");
    printf("  %s test_sgb.iq\n", progname);
    printf("  %s test_sgb.iq -s 2500000\n\n", progname);
    printf("Workflow (recommended for accuracy):\n");
    printf("  1. ./test_sample_rate file.iq          # Detect sample rate\n");
    printf("  2. ./dec406_iq file.iq -s 2500000      # Use detected rate\n\n");
    printf("Output:\n");
    printf("  - Demodulator statistics (SNR, frequency offset, etc.)\n");
    printf("  - Decoded beacon message (Country, Position, Vessel ID)\n");
    printf("  - OpenStreetMap link\n");
}

/**
 * @brief Print bits in hex format (for debugging)
 */
static void print_bits_hex(const uint8_t *bits, int length) {
    printf("\n[DEBUG] Demodulated bits (hex):\n");
    for (int i = 0; i < length; i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8 && (i + j) < length; j++) {
            byte = (byte << 1) | bits[i + j];
        }
        printf("%02X", byte);
        if ((i + 8) % 32 == 0) printf("\n");
    }
    printf("\n");
}

/**
 * @brief Print preamble analysis (for debugging)
 */
static void print_preamble_analysis(const uint8_t *bits) {
    printf("\n[DEBUG] Preamble Analysis (50 bits):\n");
    printf("Expected: 0000000000... (T.018 §2.2.4: all bits '0')\n");
    printf("Received: ");

    int errors = 0;
    for (int i = 0; i < 50; i++) {
        printf("%d", bits[i]);

        // T.018 §2.2.4: All preamble bits should be '0'
        uint8_t expected = 0;
        if (bits[i] != expected) {
            errors++;
        }

        if ((i + 1) % 10 == 0) printf(" ");
    }

    printf("\nErrors: %d/50 (%.1f%%)\n", errors, (errors * 100.0) / 50.0);
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    // Parse arguments
    const char *filename = NULL;
    float manual_sample_rate = 0.0f;  // 0 = auto-detect
    float forced_freq_offset = NAN;   // NAN = auto-search

    if (argc < 2 || argc > 6) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                manual_sample_rate = atof(argv[++i]);
                if (manual_sample_rate <= 0) {
                    fprintf(stderr, "Error: Invalid sample rate: %s\n", argv[i]);
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: -s requires sample rate argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 < argc) {
                forced_freq_offset = atof(argv[++i]);
            } else {
                fprintf(stderr, "Error: -f requires frequency offset argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Check file extension
    int len = strlen(filename);
    if (len < 3) {
        fprintf(stderr, "Error: Invalid filename\n");
        return 1;
    }

    const char *ext = filename + len - 3;
    if (strcmp(ext, ".iq") != 0 && strcmp(ext + 1, "file") != 0) {
        fprintf(stderr, "Error: Unsupported format. Use .iq or .cfile\n");
        return 1;
    }

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║     COSPAS-SARSAT 2G IQ DEMODULATOR (T.018 Rev.12)           ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    // Verify PRN generator compliance with T.018
    printf("=== PRN GENERATOR VALIDATION ===\n");
    if (!prn_verify_table_2_2()) {
        fprintf(stderr, "FATAL: PRN generator does not match T.018 specification!\n");
        return 1;
    }
    printf("\n");

    // Allocate output buffer for 300 bits
    uint8_t output_bits[DSSS_TOTAL_BITS];
    memset(output_bits, 0, sizeof(output_bits));

    // State structure for statistics
    dsss_demod_state_t state;
    memset(&state, 0, sizeof(state));

    // =========================================================================
    // STEP 1: DEMODULATE IQ FILE
    // =========================================================================
    printf("Input file: %s\n", filename);
    printf("\n");
    printf("=== DEMODULATION PROCESS ===\n");

    int ret = dsss_demodulate_file(filename, output_bits, &state, manual_sample_rate, forced_freq_offset);

    if (ret == -2) {
        fprintf(stderr, "\n❌ DEMODULATION FAILED: Preamble not found\n");
        fprintf(stderr, "\nPossible causes:\n");
        fprintf(stderr, "  - Signal too weak (low SNR)\n");
        fprintf(stderr, "  - Large frequency offset (>12 kHz)\n");
        fprintf(stderr, "  - Wrong sample rate (expected: 2.5 MHz)\n");
        fprintf(stderr, "  - Insufficient samples (need ~2.5M for 1.0 sec)\n");
        return 2;
    }

    if (ret != 0) {
        fprintf(stderr, "\n❌ DEMODULATION FAILED: Error code %d\n", ret);
        return 3;
    }

    printf("\n✅ DEMODULATION SUCCESS\n");

    // =========================================================================
    // STEP 2: DISPLAY STATISTICS
    // =========================================================================
    printf("\n");
    printf("=== DEMODULATOR STATISTICS ===\n");
    dsss_print_state(&state);

    // =========================================================================
    // STEP 3: DEBUG OUTPUT (enabled for diagnosis)
    // =========================================================================
    print_preamble_analysis(output_bits);
    print_bits_hex(output_bits, DSSS_TOTAL_BITS);

    // =========================================================================
    // STEP 4: EXTRACT PAYLOAD AND DECODE
    // =========================================================================
    printf("\n");
    printf("=== FRAME DECODING ===\n");

    // Skip 50-bit preamble, extract 250-bit payload
    uint8_t payload[DSSS_PAYLOAD_LENGTH];
    memcpy(payload, output_bits + DSSS_PREAMBLE_LENGTH, DSSS_PAYLOAD_LENGTH);

    // Pass to existing decoder (250 bits = 202 data + 48 BCH)
    // The decoder will:
    //   1. Verify BCH(250,202) checksum
    //   2. Extract 23-HEX ID, Country Code, TAC
    //   3. Decode GPS position, Vessel ID
    //   4. Display formatted output + OSM link
    decode_beacon(payload, DSSS_PAYLOAD_LENGTH);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    DEMODULATION COMPLETE                      ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
