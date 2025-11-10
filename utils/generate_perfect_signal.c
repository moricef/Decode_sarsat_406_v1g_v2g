/**
 * @file generate_perfect_signal.c
 * @brief Générateur de signal DSSS/OQPSK minimal et contrôlé
 *
 * Génère un signal synthétique parfait avec pattern de bits connu.
 * Chaque étape est validée et dumpée pour vérification.
 *
 * Étapes:
 * 1. Générer 300 bits (pattern simple: tous à 0, ou alterné)
 * 2. Spreading DSSS avec PRN (38,400 chips)
 * 3. Modulation OQPSK rectangulaire (2.5 MHz, 65.104 samples/chip)
 * 4. Sauver IQ en float32
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prn_generator.h"

#define NUM_BITS 300
#define CHIPS_PER_BIT 256
#define TOTAL_CHIPS (NUM_BITS * CHIPS_PER_BIT)  // 76,800
#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f
#define SAMPLES_PER_CHIP (SAMPLE_RATE / CHIP_RATE)  // 65.104166...

// Patterns de test disponibles
typedef enum {
    PATTERN_ALL_ZEROS,      // 300 bits à 0
    PATTERN_ALL_ONES,       // 300 bits à 1
    PATTERN_ALTERNATING,    // 01010101...
    PATTERN_PREAMBLE_ZEROS  // 50 bits à 0, puis 250 aléatoires
} pattern_type_t;

/**
 * Génère les bits selon le pattern choisi
 */
void generate_bits(uint8_t *bits, pattern_type_t pattern) {
    switch (pattern) {
    case PATTERN_ALL_ZEROS:
        memset(bits, 0, NUM_BITS);
        printf("Pattern: ALL ZEROS (300 bits)\n");
        break;

    case PATTERN_ALL_ONES:
        memset(bits, 1, NUM_BITS);
        printf("Pattern: ALL ONES (300 bits)\n");
        break;

    case PATTERN_ALTERNATING:
        for (int i = 0; i < NUM_BITS; i++) {
            bits[i] = i % 2;
        }
        printf("Pattern: ALTERNATING 0101... (300 bits)\n");
        break;

    case PATTERN_PREAMBLE_ZEROS:
        memset(bits, 0, 50);
        for (int i = 50; i < NUM_BITS; i++) {
            bits[i] = (i * 7 + 13) % 2;  // Pattern pseudo-aléatoire simple
        }
        printf("Pattern: PREAMBLE (50x0) + DATA (250 pseudo-random)\n");
        break;
    }
}

/**
 * Split bits I/Q pour OQPSK
 */
void split_iq_bits(const uint8_t *bits, uint8_t *i_bits, uint8_t *q_bits) {
    for (int i = 0; i < NUM_BITS / 2; i++) {
        i_bits[i] = bits[i * 2];
        q_bits[i] = bits[i * 2 + 1];
    }
}

/**
 * Spreading DSSS avec PRN
 */
void apply_dsss_spreading(const uint8_t *i_bits, const uint8_t *q_bits,
                         int8_t *i_chips, int8_t *q_chips) {
    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    int8_t prn_i_buf[CHIPS_PER_BIT];
    int8_t prn_q_buf[CHIPS_PER_BIT];

    // Générer PRN pour I
    for (int bit = 0; bit < 150; bit++) {
        prn_generate_i(&prn_state, prn_i_buf);

        // Appliquer bit: 0 → keep PRN, 1 → invert PRN
        for (int chip = 0; chip < CHIPS_PER_BIT; chip++) {
            int idx = bit * CHIPS_PER_BIT + chip;
            i_chips[idx] = i_bits[bit] ? -prn_i_buf[chip] : prn_i_buf[chip];
        }
    }

    // Reset PRN pour Q
    prn_init(&prn_state, 0);

    // Générer PRN pour Q
    for (int bit = 0; bit < 150; bit++) {
        prn_generate_q(&prn_state, prn_q_buf);

        for (int chip = 0; chip < CHIPS_PER_BIT; chip++) {
            int idx = bit * CHIPS_PER_BIT + chip;
            q_chips[idx] = q_bits[bit] ? -prn_q_buf[chip] : prn_q_buf[chip];
        }
    }

    printf("✓ DSSS spreading: 300 bits → 38,400 chips I/Q\n");
}

/**
 * Modulation OQPSK avec interpolation linéaire (comme SARSAT_SGB)
 */
int modulate_oqpsk_interpolated(const int8_t *i_chips, const int8_t *q_chips,
                                float *iq_samples) {
    int total_samples = 0;
    float sample_accumulator = 0.0f;
    float prev_i_chip = 0.0f;
    float prev_q_chip = 0.0f;

    printf("  Using SARSAT_SGB method: linear interpolation + half-chip OQPSK delay\n");

    for (int chip_idx = 0; chip_idx < TOTAL_CHIPS; chip_idx++) {
        float curr_i_chip = (float)i_chips[chip_idx];
        float curr_q_chip = (float)q_chips[chip_idx];

        // Calculate number of samples for this chip
        sample_accumulator += SAMPLES_PER_CHIP;
        int num_samples = (int)sample_accumulator;
        sample_accumulator -= num_samples;

        for (int s = 0; s < num_samples; s++) {
            float fraction = (float)s / num_samples;

            // I-channel: linear interpolation (SARSAT_SGB line 243)
            float i_value = prev_i_chip + (curr_i_chip - prev_i_chip) * fraction;

            // Q-channel: OQPSK half-chip delay (SARSAT_SGB lines 247-250)
            float q_value;
            if (fraction < 0.5f) {
                q_value = prev_q_chip;
            } else {
                q_value = prev_q_chip + (curr_q_chip - prev_q_chip) * (fraction - 0.5f) * 2.0f;
            }

            // Store I/Q sample
            iq_samples[total_samples * 2] = i_value;
            iq_samples[total_samples * 2 + 1] = q_value;
            total_samples++;
        }

        prev_i_chip = curr_i_chip;
        prev_q_chip = curr_q_chip;
    }

    // Handle accumulator remainder (SARSAT_SGB lines 273-279)
    if (sample_accumulator > 0.5f) {
        iq_samples[total_samples * 2] = prev_i_chip;
        iq_samples[total_samples * 2 + 1] = prev_q_chip;
        total_samples++;
        printf("  [FIX] Added 1 sample for float32 rounding (acc=%.6f)\n", sample_accumulator);
    }

    printf("✓ OQPSK modulation: %d samples (interpolated, half-chip Q delay)\n", total_samples);

    return total_samples;
}

/**
 * Sauvegarde signal IQ en float32 interleaved
 */
void save_iq_file(const char *filename, const float *iq_samples, int num_samples) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible de créer %s\n", filename);
        return;
    }

    fwrite(iq_samples, sizeof(float), num_samples * 2, f);
    fclose(f);

    float size_mb = (num_samples * 2 * sizeof(float)) / (1024.0f * 1024.0f);
    printf("✓ Fichier IQ sauvé: %s (%.1f MB, %d samples)\n", filename, size_mb, num_samples);
}

/**
 * Dump chips pour vérification
 */
void dump_chips(const char *filename, const int8_t *i_chips, const int8_t *q_chips) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible de créer %s\n", filename);
        return;
    }

    // Format: interleaved I/Q int8_t
    for (int i = 0; i < TOTAL_CHIPS; i++) {
        fwrite(&i_chips[i], sizeof(int8_t), 1, f);
        fwrite(&q_chips[i], sizeof(int8_t), 1, f);
    }

    fclose(f);
    printf("✓ Chips dumped: %s (%d bytes)\n", filename, TOTAL_CHIPS * 2);
}

/**
 * Affiche stats sur les chips
 */
void print_chip_stats(const int8_t *chips, const char *name) {
    int count_pos = 0, count_neg = 0;
    for (int i = 0; i < TOTAL_CHIPS; i++) {
        if (chips[i] > 0) count_pos++;
        else if (chips[i] < 0) count_neg++;
    }

    float balance = 100.0f * count_pos / TOTAL_CHIPS;
    printf("  %s: +1=%d (%.1f%%)  -1=%d (%.1f%%)\n",
           name, count_pos, balance, count_neg, 100.0f - balance);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pattern> [output.iq]\n", argv[0]);
        fprintf(stderr, "\nPatterns:\n");
        fprintf(stderr, "  0 - ALL ZEROS (300 bits)\n");
        fprintf(stderr, "  1 - ALL ONES (300 bits)\n");
        fprintf(stderr, "  2 - ALTERNATING 01010... (300 bits)\n");
        fprintf(stderr, "  3 - PREAMBLE (50 zeros + 250 pseudo-random)\n");
        fprintf(stderr, "\nExemple:\n");
        fprintf(stderr, "  %s 0 perfect_all_zeros.iq\n", argv[0]);
        return 1;
    }

    pattern_type_t pattern = (pattern_type_t)atoi(argv[1]);
    const char *output_file = (argc >= 3) ? argv[2] : "perfect_signal.iq";

    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║         GÉNÉRATEUR DE SIGNAL PARFAIT DSSS/OQPSK                  ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    // Étape 1: Générer bits
    printf("[1/5] Génération des bits...\n");
    uint8_t *bits = malloc(NUM_BITS);
    generate_bits(bits, pattern);

    // Afficher premiers bits
    printf("  First 20 bits: ");
    for (int i = 0; i < 20; i++) printf("%d", bits[i]);
    printf("...\n\n");

    // Étape 2: Split I/Q
    printf("[2/5] Split I/Q (OQPSK demux)...\n");
    uint8_t *i_bits = malloc(150);
    uint8_t *q_bits = malloc(150);
    split_iq_bits(bits, i_bits, q_bits);
    printf("✓ 300 bits → 150 I + 150 Q\n\n");

    // Étape 3: DSSS Spreading
    printf("[3/5] DSSS Spreading avec PRN...\n");
    int8_t *i_chips = malloc(TOTAL_CHIPS);
    int8_t *q_chips = malloc(TOTAL_CHIPS);
    apply_dsss_spreading(i_bits, q_bits, i_chips, q_chips);

    print_chip_stats(i_chips, "I-chips");
    print_chip_stats(q_chips, "Q-chips");

    // Dump chips pour vérification
    dump_chips("perfect_chips.bin", i_chips, q_chips);
    printf("\n");

    // Étape 4: Modulation OQPSK
    printf("[4/5] Modulation OQPSK...\n");
    int max_samples = (int)(TOTAL_CHIPS * SAMPLES_PER_CHIP) + 1000;
    float *iq_samples = calloc(max_samples * 2, sizeof(float));
    int num_samples = modulate_oqpsk_interpolated(i_chips, q_chips, iq_samples);
    printf("\n");

    // Étape 5: Sauvegarde
    printf("[5/5] Sauvegarde fichier IQ...\n");
    save_iq_file(output_file, iq_samples, num_samples);

    printf("\n╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ GÉNÉRATION TERMINÉE                                               ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    printf("Fichiers générés:\n");
    printf("  - %s          (signal IQ, %d samples)\n", output_file, num_samples);
    printf("  - perfect_chips.bin  (chips référence, %d bytes)\n", TOTAL_CHIPS * 2);

    printf("\nTest avec démodulateur:\n");
    printf("  ./dec406_iq %s -f 0\n", output_file);
    printf("  ./compare_tx_rx_chips perfect_chips.bin %s 0\n", output_file);

    // Cleanup
    free(bits);
    free(i_bits);
    free(q_bits);
    free(i_chips);
    free(q_chips);
    free(iq_samples);

    return 0;
}
