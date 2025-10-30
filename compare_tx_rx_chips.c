/**
 * @file compare_tx_rx_chips.c
 * @brief Compare TX reference chips with RX extracted chips
 *
 * Charge:
 * 1. chips_after_spreading.bin (TX reference, 76,800 bytes)
 * 2. Signal IQ (RX, 2.5 MHz samples)
 *
 * Extrait les chips du signal RX et compare avec la référence TX.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define TOTAL_CHIPS 38400
#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f
#define SAMPLES_PER_CHIP (SAMPLE_RATE / CHIP_RATE)

/**
 * Charge les chips TX de référence
 */
int load_tx_chips(const char *filename, int8_t *i_chips, int8_t *q_chips) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", filename);
        return -1;
    }

    // Format: interleaved I/Q
    for (int i = 0; i < TOTAL_CHIPS; i++) {
        if (fread(&i_chips[i], sizeof(int8_t), 1, f) != 1) {
            fprintf(stderr, "Erreur lecture I chip %d\n", i);
            fclose(f);
            return -1;
        }
        if (fread(&q_chips[i], sizeof(int8_t), 1, f) != 1) {
            fprintf(stderr, "Erreur lecture Q chip %d\n", i);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

/**
 * Extrait les chips du signal IQ RX
 * OQPSK: Q est retardé de Tc/2 par rapport à I
 */
int extract_rx_chips(const char *filename, int8_t *i_chips, int8_t *q_chips, int offset) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", filename);
        return -1;
    }

    // Charger tout le fichier
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t num_samples = file_size / (2 * sizeof(float));
    float *iq = malloc(file_size);
    if (!iq) {
        fprintf(stderr, "Erreur allocation mémoire\n");
        fclose(f);
        return -1;
    }

    fread(iq, 1, file_size, f);
    fclose(f);

    // OQPSK avec interpolation: moyenner les samples sur chaque chip
    // au lieu de prendre un seul sample

    // Extraire chips I (moyenner les 65 samples du chip)
    for (int chip = 0; chip < TOTAL_CHIPS; chip++) {
        int start_idx = offset + (int)(chip * SAMPLES_PER_CHIP);
        int end_idx = offset + (int)((chip + 1) * SAMPLES_PER_CHIP);

        if (end_idx >= num_samples) {
            fprintf(stderr, "Erreur I: end_idx %d >= num_samples %zu (chip %d)\n",
                    end_idx, num_samples, chip);
            free(iq);
            return -1;
        }

        // Moyenner les samples I sur le chip
        float i_sum = 0.0f;
        int count = 0;
        for (int s = start_idx; s < end_idx && s < num_samples; s++) {
            i_sum += iq[s * 2];
            count++;
        }
        float i_avg = (count > 0) ? i_sum / count : 0.0f;
        i_chips[chip] = (i_avg > 0) ? 1 : -1;
    }

    // Extraire chips Q (avec délai OQPSK half-chip)
    // Pour l'interpolation OQPSK: Q est retardé d'un demi-chip
    // Moyenner sur la deuxième moitié du chip N et première moitié du chip N+1
    for (int chip = 0; chip < TOTAL_CHIPS; chip++) {
        int chip_start = offset + (int)(chip * SAMPLES_PER_CHIP);
        int chip_mid = chip_start + (int)(SAMPLES_PER_CHIP / 2.0f);
        int chip_end = offset + (int)((chip + 1) * SAMPLES_PER_CHIP);

        if (chip_end >= num_samples) {
            fprintf(stderr, "Erreur Q: chip_end %d >= num_samples %zu (chip %d)\n",
                    chip_end, num_samples, chip);
            free(iq);
            return -1;
        }

        // Moyenner Q sur [mid_chip...end_chip]
        float q_sum = 0.0f;
        int count = 0;
        for (int s = chip_mid; s < chip_end && s < num_samples; s++) {
            q_sum += iq[s * 2 + 1];
            count++;
        }
        float q_avg = (count > 0) ? q_sum / count : 0.0f;
        q_chips[chip] = (q_avg > 0) ? 1 : -1;
    }

    free(iq);
    return 0;
}

/**
 * Compare deux séquences de chips
 */
void compare_chips(const int8_t *tx, const int8_t *rx, int num_chips, const char *channel_name) {
    int matches = 0;
    int first_error = -1;
    int last_error = -1;

    for (int i = 0; i < num_chips; i++) {
        if (tx[i] == rx[i]) {
            matches++;
        } else {
            if (first_error == -1) first_error = i;
            last_error = i;
        }
    }

    float match_percent = 100.0f * matches / num_chips;

    printf("\n%s Channel:\n", channel_name);
    printf("  Matches: %d / %d (%.2f%%)\n", matches, num_chips, match_percent);

    if (match_percent < 100.0f) {
        printf("  First error: chip %d (TX=%d, RX=%d)\n", first_error, tx[first_error], rx[first_error]);
        printf("  Last error:  chip %d (TX=%d, RX=%d)\n", last_error, tx[last_error], rx[last_error]);

        // Montrer pattern autour de la première erreur
        if (first_error >= 5 && first_error < num_chips - 5) {
            printf("  Context (chip %d ±5):\n", first_error);
            printf("    TX: ");
            for (int i = first_error - 5; i <= first_error + 5; i++) {
                printf("%2d ", tx[i]);
            }
            printf("\n    RX: ");
            for (int i = first_error - 5; i <= first_error + 5; i++) {
                printf("%2d ", rx[i]);
            }
            printf("\n");
        }
    }
}

/**
 * Analyse par groupes de bits (256 chips/bit)
 */
void analyze_by_bits(const int8_t *tx_i, const int8_t *tx_q,
                     const int8_t *rx_i, const int8_t *rx_q) {
    printf("\n╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║              ANALYSE PAR BITS (300 bits)                          ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    int total_bits = TOTAL_CHIPS / 256;

    printf("Bit |  I Match  |  Q Match  | Combined\n");
    printf("----+-----------+-----------+---------\n");

    int preamble_i_match = 0, preamble_q_match = 0;
    int data_i_match = 0, data_q_match = 0;

    for (int bit = 0; bit < total_bits; bit++) {
        int start = bit * 256;
        int end = start + 256;

        int i_matches = 0, q_matches = 0;
        for (int chip = start; chip < end; chip++) {
            if (tx_i[chip] == rx_i[chip]) i_matches++;
            if (tx_q[chip] == rx_q[chip]) q_matches++;
        }

        float i_percent = 100.0f * i_matches / 256;
        float q_percent = 100.0f * q_matches / 256;
        float combined = (i_percent + q_percent) / 2.0f;

        // Accumulate stats
        if (bit < 50) {  // Preamble
            preamble_i_match += i_matches;
            preamble_q_match += q_matches;
        } else {  // Data
            data_i_match += i_matches;
            data_q_match += q_matches;
        }

        // Afficher toutes les 10 lignes ou si erreur
        if (bit % 10 == 0 || combined < 90.0f) {
            printf("%3d | %5.1f%%    | %5.1f%%    | %5.1f%%\n",
                   bit, i_percent, q_percent, combined);
        }
    }

    printf("\n╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ RÉSUMÉ PAR ZONE                                                   ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");

    float preamble_i = 100.0f * preamble_i_match / (50 * 256);
    float preamble_q = 100.0f * preamble_q_match / (50 * 256);
    float data_i = 100.0f * data_i_match / (250 * 256);
    float data_q = 100.0f * data_q_match / (250 * 256);

    printf("\nPréambule (bits 0-49):\n");
    printf("  I: %.2f%%  Q: %.2f%%  Combined: %.2f%%\n",
           preamble_i, preamble_q, (preamble_i + preamble_q) / 2.0f);

    printf("\nDonnées (bits 50-299):\n");
    printf("  I: %.2f%%  Q: %.2f%%  Combined: %.2f%%\n",
           data_i, data_q, (data_i + data_q) / 2.0f);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <chips_tx.bin> <signal_rx.iq> [offset]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Exemple:\n");
        fprintf(stderr, "  %s chips_after_spreading.bin test.iq 0\n", argv[0]);
        return 1;
    }

    const char *tx_file = argv[1];
    const char *rx_file = argv[2];
    int offset = (argc >= 4) ? atoi(argv[3]) : 0;

    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║         COMPARAISON TX vs RX CHIPS                                ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");
    printf("TX reference: %s\n", tx_file);
    printf("RX signal:    %s\n", rx_file);
    printf("RX offset:    %d samples\n\n", offset);

    // Allouer mémoire
    int8_t *tx_i = malloc(TOTAL_CHIPS);
    int8_t *tx_q = malloc(TOTAL_CHIPS);
    int8_t *rx_i = malloc(TOTAL_CHIPS);
    int8_t *rx_q = malloc(TOTAL_CHIPS);

    if (!tx_i || !tx_q || !rx_i || !rx_q) {
        fprintf(stderr, "Erreur allocation mémoire\n");
        return 1;
    }

    // Charger TX reference
    printf("Chargement TX reference...\n");
    if (load_tx_chips(tx_file, tx_i, tx_q) < 0) {
        return 1;
    }
    printf("  ✓ %d chips I/Q chargés\n", TOTAL_CHIPS);

    // Extraire RX chips
    printf("Extraction RX chips...\n");
    if (extract_rx_chips(rx_file, rx_i, rx_q, offset) < 0) {
        return 1;
    }
    printf("  ✓ %d chips I/Q extraits\n\n", TOTAL_CHIPS);

    // Comparer
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ COMPARAISON CHIP PAR CHIP                                         ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");

    compare_chips(tx_i, rx_i, TOTAL_CHIPS, "I");
    compare_chips(tx_q, rx_q, TOTAL_CHIPS, "Q");

    // Analyse par bits
    analyze_by_bits(tx_i, tx_q, rx_i, rx_q);

    // Cleanup
    free(tx_i);
    free(tx_q);
    free(rx_i);
    free(rx_q);

    return 0;
}
