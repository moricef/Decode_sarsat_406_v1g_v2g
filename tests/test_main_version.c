// Quick test for main branch dsss_demod API
#include "dsss_demod.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <iq_file>\n", argv[0]);
        return 1;
    }

    uint8_t output_bits[300];
    dsss_demod_state_t state = {0};

    printf("=== Testing MAIN branch version on %s ===\n\n", argv[1]);

    int result = dsss_demodulate_file(argv[1], output_bits, &state);

    if (result == 0) {
        printf("\n=== Demodulation SUCCESS ===\n");
        dsss_print_state(&state);

        printf("\n=== Decoded bits (first 250) ===\n");
        for (int i = 0; i < 250 && i < 300; i++) {
            if (i > 0 && i % 10 == 0) printf(" ");
            if (i > 0 && i % 50 == 0) printf("\n");
            printf("%d", output_bits[i]);
        }
        printf("\n");
    } else {
        printf("\n=== Demodulation FAILED ===\n");
        dsss_print_state(&state);
    }

    return result;
}
