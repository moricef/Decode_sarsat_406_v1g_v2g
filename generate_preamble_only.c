/**
 * @file generate_preamble_only.c
 * @brief Générateur de signal préambule uniquement (50 bits à 0)
 *
 * Génère un signal DSSS/OQPSK ultra-court avec seulement le préambule.
 * 50 bits × 256 chips/bit = 12,800 chips = ~833,000 samples
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prn_generator.h"

#define NUM_BITS 50
#define CHIPS_PER_BIT 256
#define TOTAL_CHIPS (NUM_BITS * CHIPS_PER_BIT)  // 12,800
#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f
#define SAMPLES_PER_CHIP (SAMPLE_RATE / CHIP_RATE)  // 65.104166...

int main(int argc, char *argv[]) {
    const char *output_file = (argc >= 2) ? argv[1] : "preamble_only.iq";

    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║         GÉNÉRATEUR PRÉAMBULE SEULEMENT (50 bits)                 ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    // Générer 50 bits à 0
    printf("[1/4] Génération préambule (50 bits à 0)...\n");
    uint8_t bits[NUM_BITS];
    memset(bits, 0, NUM_BITS);
    printf("✓ 50 bits: 00000000000000000000000000000000000000000000000000\n\n");

    // Split I/Q (25 bits chacun)
    printf("[2/4] Split I/Q...\n");
    uint8_t i_bits[25], q_bits[25];
    for (int i = 0; i < 25; i++) {
        i_bits[i] = bits[i * 2];
        q_bits[i] = bits[i * 2 + 1];
    }
    printf("✓ 50 bits → 25 I + 25 Q\n\n");

    // DSSS Spreading
    printf("[3/4] DSSS Spreading...\n");
    int8_t *i_chips = malloc(TOTAL_CHIPS);
    int8_t *q_chips = malloc(TOTAL_CHIPS);

    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    int8_t prn_buf[CHIPS_PER_BIT];

    // I-channel
    for (int bit = 0; bit < 25; bit++) {
        prn_generate_i(&prn_state, prn_buf);
        for (int chip = 0; chip < CHIPS_PER_BIT; chip++) {
            int idx = bit * CHIPS_PER_BIT + chip;
            i_chips[idx] = i_bits[bit] ? -prn_buf[chip] : prn_buf[chip];
        }
    }

    // Q-channel
    prn_init(&prn_state, 0);
    for (int bit = 0; bit < 25; bit++) {
        prn_generate_q(&prn_state, prn_buf);
        for (int chip = 0; chip < CHIPS_PER_BIT; chip++) {
            int idx = bit * CHIPS_PER_BIT + chip;
            q_chips[idx] = q_bits[bit] ? -prn_buf[chip] : prn_buf[chip];
        }
    }
    printf("✓ 12,800 chips I/Q générés\n\n");

    // Dump chips
    FILE *chip_dump = fopen("preamble_chips.bin", "wb");
    if (chip_dump) {
        for (int i = 0; i < TOTAL_CHIPS; i++) {
            fwrite(&i_chips[i], sizeof(int8_t), 1, chip_dump);
            fwrite(&q_chips[i], sizeof(int8_t), 1, chip_dump);
        }
        fclose(chip_dump);
        printf("✓ Chips sauvés: preamble_chips.bin (%d bytes)\n\n", TOTAL_CHIPS * 2);
    }

    // Modulation OQPSK rectangulaire
    printf("[4/4] Modulation OQPSK...\n");
    const int Q_DELAY_SAMPLES = (int)(SAMPLES_PER_CHIP / 2.0f);
    int max_samples = (int)(TOTAL_CHIPS * SAMPLES_PER_CHIP) + 1000;

    float *i_samples_full = calloc(max_samples, sizeof(float));
    float *q_samples_full = calloc(max_samples, sizeof(float));
    float *iq_samples = calloc(max_samples * 2, sizeof(float));

    // Générer I samples
    int i_sample_idx = 0;
    float sample_accumulator = 0.0f;
    for (int chip = 0; chip < TOTAL_CHIPS; chip++) {
        float chip_value = (float)i_chips[chip];
        sample_accumulator += SAMPLES_PER_CHIP;
        int num_samples = (int)sample_accumulator;
        sample_accumulator -= num_samples;
        for (int s = 0; s < num_samples; s++) {
            i_samples_full[i_sample_idx++] = chip_value;
        }
    }

    // Générer Q samples
    int q_sample_idx = 0;
    sample_accumulator = 0.0f;
    for (int chip = 0; chip < TOTAL_CHIPS; chip++) {
        float chip_value = (float)q_chips[chip];
        sample_accumulator += SAMPLES_PER_CHIP;
        int num_samples = (int)sample_accumulator;
        sample_accumulator -= num_samples;
        for (int s = 0; s < num_samples; s++) {
            q_samples_full[q_sample_idx++] = chip_value;
        }
    }

    // Combiner avec délai OQPSK
    int total_samples = 0;
    for (int s = 0; s < i_sample_idx; s++) {
        int q_idx = s - Q_DELAY_SAMPLES;
        float q_val = (q_idx >= 0 && q_idx < q_sample_idx) ? q_samples_full[q_idx] : 0.0f;
        iq_samples[total_samples * 2] = i_samples_full[s];
        iq_samples[total_samples * 2 + 1] = q_val;
        total_samples++;
    }

    printf("✓ %d samples générés (Q delayed by %d samples)\n", total_samples, Q_DELAY_SAMPLES);

    // Sauvegarder
    FILE *f = fopen(output_file, "wb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible de créer %s\n", output_file);
        return 1;
    }
    fwrite(iq_samples, sizeof(float), total_samples * 2, f);
    fclose(f);

    float size_mb = (total_samples * 2 * sizeof(float)) / (1024.0f * 1024.0f);
    printf("✓ Fichier sauvé: %s (%.2f MB)\n\n", output_file, size_mb);

    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ FICHIERS GÉNÉRÉS                                                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");
    printf("  %s     (%d samples, %.2f MB)\n", output_file, total_samples, size_mb);
    printf("  preamble_chips.bin  (%d bytes)\n\n", TOTAL_CHIPS * 2);

    printf("Test démodulateur:\n");
    printf("  ./dec406_iq %s -f 0\n\n", output_file);

    printf("Comparaison chips:\n");
    printf("  ./compare_tx_rx_chips preamble_chips.bin %s 0\n", output_file);

    // Cleanup
    free(i_chips);
    free(q_chips);
    free(i_samples_full);
    free(q_samples_full);
    free(iq_samples);

    return 0;
}
