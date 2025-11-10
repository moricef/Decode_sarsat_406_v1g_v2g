#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "dsss_demod.h"

#define M_PI 3.14159265358979323846

int main() {
    // Load generated signal
    FILE *f = fopen("/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/test_signal_fixed.iq", "rb");
    if (!f) {
        printf("ERROR: Cannot open signal file\n");
        return 1;
    }

    // Read first 64×64 = 4096 samples (first chip at SPS=64)
    float complex *signal = malloc(4096 * sizeof(float complex));
    for (int i = 0; i < 4096; i++) {
        float re, im;
        fread(&re, sizeof(float), 1, f);
        fread(&im, sizeof(float), 1, f);
        signal[i] = re + I*im;
    }
    fclose(f);

    // Generate expected reference
    int8_t *prn_i = malloc(DSSS_PACKET_CHIPS);
    int8_t *prn_q = malloc(DSSS_PACKET_CHIPS);
    dsss_generate_prn_sequences(prn_i, prn_q);

    printf("=== Signal Comparison (First Chip) ===\n\n");

    // Show first 10 samples
    printf("First 10 samples of generated signal:\n");
    for (int i = 0; i < 10; i++) {
        printf("  [%d] %.4f %+.4fj (mag=%.4f)\n", i,
               crealf(signal[i]), cimagf(signal[i]), cabsf(signal[i]));
    }

    // Expected first chip values
    float chip_i = (prn_i[0] > 0) ? 1.0f : -1.0f;
    float chip_q = (prn_q[0] > 0) ? 1.0f : -1.0f;
    float complex expected = (chip_i + I*chip_q) * cexpf(I * M_PI / 4.0f) / sqrtf(2.0f);

    printf("\nExpected first chip (PRN_I=%+d, PRN_Q=%+d):\n", prn_i[0], prn_q[0]);
    printf("  Before rotation: %.4f %+.4fj\n", chip_i, chip_q);
    printf("  After π/4 rot + norm: %.4f %+.4fj (mag=%.4f)\n",
           crealf(expected), cimagf(expected), cabsf(expected));

    // Convert OQPSK to QPSK
    printf("\n=== OQPSK → QPSK Conversion ===\n");
    int q_delay = 32;  // SPS/2
    for (int i = 0; i < 10; i++) {
        float complex qpsk = crealf(signal[i]) + I*cimagf(signal[i + q_delay]);
        printf("  [%d] OQPSK: I[%d]=%.4f, Q[%d]=%.4f → QPSK: %.4f %+.4fj\n",
               i, i, crealf(signal[i]), i+q_delay, cimagf(signal[i+q_delay]),
               crealf(qpsk), cimagf(qpsk));
    }

    // Check match at different symbol positions
    printf("\n=== Correlation at Symbol Rate ===\n");
    for (int sym = 0; sym < 10; sym++) {
        // Decimate at SPS=64
        float complex rx = crealf(signal[sym*64]) + I*cimagf(signal[sym*64 + 32]);

        float chip_i_ref = (prn_i[sym] > 0) ? 1.0f : -1.0f;
        float chip_q_ref = (prn_q[sym] > 0) ? 1.0f : -1.0f;
        float complex ref = (chip_i_ref + I*chip_q_ref) * cexpf(I * M_PI / 4.0f) / sqrtf(2.0f);

        float complex corr = rx * conjf(ref);

        printf("  Symbol %d: RX=(%.3f%+.3fj) REF=(%.3f%+.3fj) CORR=%.3f\n",
               sym, crealf(rx), cimagf(rx), crealf(ref), cimagf(ref), crealf(corr));
    }

    free(signal);
    free(prn_i);
    free(prn_q);

    return 0;
}
