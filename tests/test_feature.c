#include "dsss_demod.h"
#include <stdio.h>
#include <stdlib.h>
#include <complex.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <iq_file>\n", argv[0]);
        return 1;
    }

    // Load IQ file
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t num_samples = file_size / (2 * sizeof(float));
    printf("Loading IQ file: %s\n", argv[1]);
    printf("  File size: %ld bytes\n", file_size);
    printf("  Samples: %zu (%.3f seconds @ 2.5 MHz)\n",
           num_samples, (double)num_samples / 2.5e6);

    float complex *buffer = malloc(num_samples * sizeof(float complex));
    if (!buffer) {
        perror("malloc");
        fclose(f);
        return 1;
    }

    // Read interleaved I/Q
    for (size_t i = 0; i < num_samples; i++) {
        float re, im;
        fread(&re, sizeof(float), 1, f);
        fread(&im, sizeof(float), 1, f);
        buffer[i] = re + im * _Complex_I;
    }
    fclose(f);

    // Demodulate
    uint8_t output_bits[250];
    printf("\n=== Testing FEATURE branch version ===\n\n");
    int result = dsss_receive_burst(buffer, num_samples, 65, 2.5e6, 12000, output_bits);

    if (result == 0) {
        printf("\n✓ Demodulation SUCCESS\n");
        printf("\n=== Decoded bits (first 250) ===\n");
        for (int i = 0; i < 250; i++) {
            printf("%d", output_bits[i]);
            if ((i + 1) % 10 == 0) printf(" ");
            if ((i + 1) % 50 == 0) printf("\n");
        }
        printf("\n");
    } else {
        printf("\n✗ Demodulation FAILED\n");
    }

    free(buffer);
    return result;
}
