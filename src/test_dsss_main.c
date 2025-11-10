/**
 * @file test_dsss_main.c
 * @brief Test program for DSSS OQPSK demodulator
 *
 * Loads IQ file, demodulates DSSS signal, and decodes 2G beacon message
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <complex.h>
#include "dsss_demod.h"
#include "dec406.h"

/**
 * @brief Load IQ samples from file
 * @param filename Path to IQ file
 * @param samples Output buffer for complex samples
 * @param max_samples Maximum number of samples to read
 * @param format File format: "cf32" (complex float32), "cs16" (complex int16), "cu8" (complex uint8)
 * @return Number of samples read, or -1 on error
 */
static ssize_t load_iq_file(const char *filename, float complex **samples,
                            size_t max_samples, const char *format) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    size_t num_samples;

    if (strcmp(format, "cf32") == 0) {
        // Complex float32 (8 bytes per sample: 4 bytes I + 4 bytes Q)
        num_samples = file_size / 8;
        if (num_samples > max_samples) num_samples = max_samples;

        *samples = malloc(num_samples * sizeof(float complex));
        if (!*samples) {
            fprintf(stderr, "ERROR: Memory allocation failed\n");
            fclose(fp);
            return -1;
        }

        size_t read = fread(*samples, sizeof(float complex), num_samples, fp);
        fclose(fp);

        printf("Loaded %zu samples (cf32 format)\n", read);
        return read;

    } else if (strcmp(format, "cs16") == 0) {
        // Complex int16 (4 bytes per sample: 2 bytes I + 2 bytes Q)
        num_samples = file_size / 4;
        if (num_samples > max_samples) num_samples = max_samples;

        int16_t *temp = malloc(num_samples * 2 * sizeof(int16_t));
        if (!temp) {
            fprintf(stderr, "ERROR: Memory allocation failed\n");
            fclose(fp);
            return -1;
        }

        size_t read = fread(temp, sizeof(int16_t), num_samples * 2, fp);
        fclose(fp);

        *samples = malloc(num_samples * sizeof(float complex));
        if (!*samples) {
            fprintf(stderr, "ERROR: Memory allocation failed\n");
            free(temp);
            return -1;
        }

        // Convert to float complex and normalize
        for (size_t i = 0; i < num_samples; i++) {
            float re = temp[2*i] / 32768.0f;
            float im = temp[2*i + 1] / 32768.0f;
            (*samples)[i] = re + I * im;
        }

        free(temp);
        printf("Loaded %zu samples (cs16 format)\n", read / 2);
        return read / 2;

    } else if (strcmp(format, "cu8") == 0) {
        // Complex uint8 (2 bytes per sample: 1 byte I + 1 byte Q)
        num_samples = file_size / 2;
        if (num_samples > max_samples) num_samples = max_samples;

        uint8_t *temp = malloc(num_samples * 2);
        if (!temp) {
            fprintf(stderr, "ERROR: Memory allocation failed\n");
            fclose(fp);
            return -1;
        }

        size_t read = fread(temp, 1, num_samples * 2, fp);
        fclose(fp);

        *samples = malloc(num_samples * sizeof(float complex));
        if (!*samples) {
            fprintf(stderr, "ERROR: Memory allocation failed\n");
            free(temp);
            return -1;
        }

        // Convert to float complex and normalize (127.5 offset, /127.5 scale)
        for (size_t i = 0; i < num_samples; i++) {
            float re = (temp[2*i] - 127.5f) / 127.5f;
            float im = (temp[2*i + 1] - 127.5f) / 127.5f;
            (*samples)[i] = re + I * im;
        }

        free(temp);
        printf("Loaded %zu samples (cu8 format)\n", read / 2);
        return read / 2;

    } else {
        fprintf(stderr, "ERROR: Unknown format '%s'. Use: cf32, cs16, or cu8\n", format);
        fclose(fp);
        return -1;
    }
}

/**
 * @brief Detect file format from extension
 */
static const char* detect_format(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "cf32";  // Default

    if (strcmp(ext, ".cfile") == 0) return "cf32";
    if (strcmp(ext, ".iq") == 0) return "cf32";
    if (strcmp(ext, ".cs16") == 0) return "cs16";
    if (strcmp(ext, ".cu8") == 0) return "cu8";
    if (strcmp(ext, ".raw") == 0) return "cu8";  // Assume RTL-SDR format

    return "cf32";  // Default to float32
}

int main(int argc, char *argv[]) {
    printf("=== COSPAS-SARSAT DSSS OQPSK Demodulator Test ===\n\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <iq_file> [format] [sps] [fs] [max_doppler]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Arguments:\n");
        fprintf(stderr, "  iq_file      - Input IQ file (.iq, .cfile, .cs16, .cu8, .raw)\n");
        fprintf(stderr, "  format       - File format: cf32, cs16, cu8 (default: auto-detect)\n");
        fprintf(stderr, "  sps          - Samples per symbol (default: 8)\n");
        fprintf(stderr, "  fs           - Sampling frequency in Hz (default: 307200)\n");
        fprintf(stderr, "  max_doppler  - Max Doppler shift in Hz (default: 12000)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "  %s signal.iq cf32 8 307200 12000\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    const char *format = (argc > 2) ? argv[2] : detect_format(filename);
    int sps = (argc > 3) ? atoi(argv[3]) : 8;
    float fs = (argc > 4) ? atof(argv[4]) : 307200.0f;
    int max_doppler = (argc > 5) ? atoi(argv[5]) : 12000;

    printf("Configuration:\n");
    printf("  File: %s\n", filename);
    printf("  Format: %s\n", format);
    printf("  Samples/symbol: %d\n", sps);
    printf("  Sampling rate: %.0f Hz\n", fs);
    printf("  Max Doppler: %d Hz\n", max_doppler);
    printf("\n");

    // Load IQ file
    float complex *samples = NULL;
    size_t max_samples = 10 * 1024 * 1024;  // 10M samples max
    ssize_t num_samples = load_iq_file(filename, &samples, max_samples, format);

    if (num_samples <= 0) {
        fprintf(stderr, "ERROR: Failed to load IQ file\n");
        return 1;
    }

    // Allocate output buffer for decoded bits
    uint8_t *output_bits = calloc(250, sizeof(uint8_t));
    if (!output_bits) {
        fprintf(stderr, "ERROR: Memory allocation failed\n");
        free(samples);
        return 1;
    }

    // Run DSSS demodulator
    printf("\n=== Starting DSSS Demodulation ===\n");
    int result = dsss_receive_burst(samples, num_samples, sps, fs, max_doppler, output_bits);

    if (result != 0) {
        fprintf(stderr, "\nERROR: Demodulation failed\n");
        free(samples);
        free(output_bits);
        return 1;
    }

    // Print received bits
    printf("\n=== Decoded Bits (first 250 bits) ===\n");
    for (int i = 0; i < 250; i++) {
        printf("%d", output_bits[i]);
        if ((i + 1) % 50 == 0) printf("\n");
        else if ((i + 1) % 10 == 0) printf(" ");
    }
    printf("\n");

    // Call 2G decoder
    printf("\n=== Decoding 2G Message ===\n");
    extern void decode_2g(const uint8_t *rx_bits);
    decode_2g(output_bits);

    // Cleanup
    free(samples);
    free(output_bits);

    printf("\n=== Test Complete ===\n");
    return 0;
}
