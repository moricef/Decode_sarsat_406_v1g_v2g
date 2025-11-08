#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "dsss_demod.h"

int main() {
    // Generate PRN sequences using demodulator code
    int8_t *prn_i_demod = malloc(DSSS_PACKET_CHIPS);
    int8_t *prn_q_demod = malloc(DSSS_PACKET_CHIPS);

    dsss_generate_prn_sequences(prn_i_demod, prn_q_demod);

    printf("Demodulator PRN sequences generated: %d chips each\n", DSSS_PACKET_CHIPS);

    // Load generator PRN chips
    FILE *f = fopen("/home/fab2/Developpement/COSPAS-SARSAT/ADALM-PLUTO/SARSAT_SGB/chips_after_spreading.bin", "rb");
    if (!f) {
        printf("ERROR: Cannot open chips_after_spreading.bin\n");
        return 1;
    }

    // Format: interleaved I/Q chips as int8_t
    int8_t *prn_i_gen = malloc(38400);
    int8_t *prn_q_gen = malloc(38400);

    for (int i = 0; i < 38400; i++) {
        fread(&prn_i_gen[i], sizeof(int8_t), 1, f);
        fread(&prn_q_gen[i], sizeof(int8_t), 1, f);
    }
    fclose(f);

    printf("Generator PRN chips loaded: 38400 chips each\n\n");

    // Compare first 64 chips
    printf("First 64 chips comparison:\n");
    printf("I channel:\n");
    printf("  Demod: ");
    for (int i = 0; i < 64; i++) printf("%+d ", prn_i_demod[i]);
    printf("\n  Gen:   ");
    for (int i = 0; i < 64; i++) printf("%+d ", prn_i_gen[i]);
    printf("\n\n");

    printf("Q channel:\n");
    printf("  Demod: ");
    for (int i = 0; i < 64; i++) printf("%+d ", prn_q_demod[i]);
    printf("\n  Gen:   ");
    for (int i = 0; i < 64; i++) printf("%+d ", prn_q_gen[i]);
    printf("\n\n");

    // Count differences
    int diff_i = 0, diff_q = 0;
    for (int i = 0; i < 38400; i++) {
        if (prn_i_demod[i] != prn_i_gen[i]) diff_i++;
        if (prn_q_demod[i] != prn_q_gen[i]) diff_q++;
    }

    printf("Differences over 38400 chips:\n");
    printf("  I channel: %d differences (%.2f%%)\n", diff_i, 100.0*diff_i/38400);
    printf("  Q channel: %d differences (%.2f%%)\n", diff_q, 100.0*diff_q/38400);

    if (diff_i == 0 && diff_q == 0) {
        printf("\n✅ PRN sequences MATCH perfectly!\n");
    } else {
        printf("\n❌ PRN sequences DIFFER!\n");

        // Show first difference
        for (int i = 0; i < 38400; i++) {
            if (prn_i_demod[i] != prn_i_gen[i]) {
                printf("  First I diff at chip %d: demod=%+d, gen=%+d\n",
                       i, prn_i_demod[i], prn_i_gen[i]);
                break;
            }
        }
        for (int i = 0; i < 38400; i++) {
            if (prn_q_demod[i] != prn_q_gen[i]) {
                printf("  First Q diff at chip %d: demod=%+d, gen=%+d\n",
                       i, prn_q_demod[i], prn_q_gen[i]);
                break;
            }
        }
    }

    free(prn_i_demod);
    free(prn_q_demod);
    free(prn_i_gen);
    free(prn_q_gen);

    return 0;
}
