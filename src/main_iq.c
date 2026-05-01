/**
 * @file main_iq.c
 * @brief COSPAS-SARSAT 2G IQ file demodulator entry point.
 *
 * Reads float complex IQ, runs the DSSS OQPSK demodulator chain
 * (dsss_demod.c), and passes the 250-bit output to the 2G decoder.
 *
 * Usage:  ./dec406_iq <file> [-s sample_rate]
 *
 * Note: only the first ~1 s of the file is processed.  A sliding-
 * window scan is needed for files with unknown burst timing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "dsss_demod.h"
#include "dec406.h"

// Stub for decode_beacon (to be implemented)
// FIX: Use decode_beacon() from dec406.c (full implementation)
// void decode_beacon(const uint8_t *bits, int length) {
//     printf("[INFO] decode_beacon() stub: would decode %d bits\n", length);
//     // TODO: Implement full BCH decoder and message parser
// }

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
    /* dec406_hex 2G convention: 63 hex chars = 252 bits = 2 leading
     * padding bits + 250 BCH frame bits. Prepend "00" so the printed
     * string can be passed directly to dec406_hex / the official decoder. */
    printf("\n[DEBUG] Demodulated bits (hex, dec406_hex format, 63 chars):\n");
    if (length == 250) {
        /* Build 252-bit padded stream: 2 zero pads + 250 message bits. */
        for (int i = 0; i < 252; i += 4) {
            uint8_t nib = 0;
            for (int j = 0; j < 4; j++) {
                int idx = i + j - 2;  /* shift by leading 2-pad */
                int b = (idx < 0 || idx >= length) ? 0 : bits[idx];
                nib = (uint8_t)((nib << 1) | b);
            }
            printf("%X", nib);
        }
        printf("\n");
        return;
    }
    /* Fallback: original byte-packed format. */
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
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    // Parse arguments
    const char *filename = NULL;
    float manual_sample_rate = 0.0f;  // 0 = auto-detect

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
            /* -f <freq>: ignored in v1 (Stage A has no Doppler comp). */
            if (i + 1 < argc) {
                (void)atof(argv[++i]);
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

    /* Accept any extension: .iq, .cfile, .sigmf-data, .raw — the loader
     * just reads cf32_le interleaved samples. */

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║     COSPAS-SARSAT 2G IQ DEMODULATOR (T.018 Rev.12)           ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Allocate output buffer for the 250-bit message (202 data + 48 BCH).
     * Preamble bits (50 zeros) are stripped by the despreader. */
    uint8_t output_bits[DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS];
    memset(output_bits, 0, sizeof(output_bits));

    // =========================================================================
    // STEP 1: LOAD IQ FILE
    // =========================================================================
    printf("Input file: %s\n", filename);
    printf("\n");
    printf("=== LOADING IQ FILE ===\n");

    /* Default to 2.4576 MHz if no -s given; override with manual_sample_rate. */
    float fs = (manual_sample_rate > 0.0f) ? manual_sample_rate : 2457600.0f;
    int sps = (int)(fs / 38400.0f + 0.5f);
    if (fabsf(fs / (float)sps - 38400.0f) > 5.0f) {
        fprintf(stderr,
                "ERROR: sample rate %.0f Hz does not yield integer SPS "
                "(fs/38400 = %.3f). Resample first.\n",
                (double)fs, (double)(fs / 38400.0f));
        return 1;
    }
    printf("Sample rate: %.0f Hz, SPS=%d, chip rate=38400 Hz\n", (double)fs, sps);

    /* Allocate ~1.1 s of data (enough for the 1.0 s burst + margin). */
    size_t required_samples = (size_t)(fs * 1.1f);
    /* Align to a multiple of sps to avoid partial stride at buffer end. */
    required_samples += (size_t)sps - (required_samples % (size_t)sps);
    float complex *iq_buffer = calloc(required_samples, sizeof(float complex));
    if (!iq_buffer) {
        fprintf(stderr, "ERROR: Cannot allocate IQ buffer\n");
        return 1;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        free(iq_buffer);
        return 1;
    }

    size_t samples_read = fread(iq_buffer, sizeof(float complex), required_samples, fp);
    fclose(fp);

    if (samples_read < (size_t)fs) {
        fprintf(stderr, "ERROR: File has only %zu samples (need >= %.0f)\n",
                samples_read, (double)fs);
        free(iq_buffer);
        return 1;
    }

    printf("Loaded %zu samples\n", samples_read);

    // =========================================================================
    // STEP 2: DEMODULATE WITH MATLAB CODER
    // =========================================================================
    printf("\n=== DEMODULATION PROCESS ===\n");

    /* Pass the full allocated buffer (zero-padded tail past samples_read) so
     * symbol_sync can produce the extra chip sample needed when off_Q != 0. */
    int ret = dsss_receive_burst(iq_buffer, required_samples, sps, fs, 0, output_bits);
    free(iq_buffer);

    if (ret == -2) {
        fprintf(stderr, "\n❌ DEMODULATION FAILED: Preamble not found\n");
        fprintf(stderr, "\nPossible causes:\n");
        fprintf(stderr, "  - Signal too weak (low SNR)\n");
        fprintf(stderr, "  - Large frequency offset (>12 kHz)\n");
        fprintf(stderr, "  - Wrong sample rate (use -s to specify)\n");
        fprintf(stderr, "  - Insufficient samples (need ~2.5M for 1.0 sec)\n");
        return 2;
    }

    if (ret != 0) {
        fprintf(stderr, "\n❌ DEMODULATION FAILED: Error code %d\n", ret);
        return 3;
    }

    printf("\n✅ DEMODULATION SUCCESS\n");

    /* Debug dump of the 250 message bits. */
    print_bits_hex(output_bits, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);

    printf("\n");
    printf("=== FRAME DECODING ===\n");

    /* The despreader output is the 250-bit payload (202 data + 48 BCH).
     * Pass straight to the existing decoder: BCH check + T.018 parsing. */
    decode_beacon(output_bits, DSSS_PAYLOAD_BITS + DSSS_PARITY_BITS);

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    DEMODULATION COMPLETE                      ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
