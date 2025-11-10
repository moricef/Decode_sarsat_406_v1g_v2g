/**
 * @file analyze_bit_by_bit_correlation.c
 * @brief Analyse corrélation bit par bit pour identifier où la démodulation échoue
 *
 * Génère un graphique ASCII de la corrélation pour chaque bit (0-299)
 * pour déterminer si la chute est brutale (bit 50→51) ou progressive (drift)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "prn_generator.h"
#include "dsss_demod.h"

#define CHIPS_PER_BIT 256
#define TOTAL_BITS 300
#define TOTAL_CHIPS (TOTAL_BITS * CHIPS_PER_BIT)

// Paramètres détectés par le démodulateur (à forcer pour cette analyse)
typedef struct {
    int phase_angle;      // Angle de rotation (deg)
    int chip_offset;      // Offset en chips
    int swap_iq;          // Swap I/Q
    int invert_i;         // Inverser I
    int invert_q;         // Inverser Q
} demod_params_t;

/**
 * Calcule la corrélation pour un seul bit
 */
float correlate_single_bit(const int8_t *rx_i_chips, const int8_t *rx_q_chips,
                           const int8_t *prn_i, const int8_t *prn_q,
                           int bit_index, int chip_offset) {
    int start_chip = bit_index * CHIPS_PER_BIT + chip_offset;
    if (start_chip < 0 || start_chip + CHIPS_PER_BIT > TOTAL_CHIPS) {
        return 0.0f;
    }

    // XOR puis compte
    int i_matches = 0;
    int q_matches = 0;

    for (int c = 0; c < CHIPS_PER_BIT; c++) {
        int chip_pos = start_chip + c;
        int prn_pos = bit_index * CHIPS_PER_BIT + c;

        // XOR pour désétalement
        int i_xor = (rx_i_chips[chip_pos] > 0 ? 1 : 0) ^ (prn_i[prn_pos] > 0 ? 1 : 0);
        int q_xor = (rx_q_chips[chip_pos] > 0 ? 1 : 0) ^ (prn_q[prn_pos] > 0 ? 1 : 0);

        // Compte les 0 (accord) vs 1 (désaccord)
        if (i_xor == 0) i_matches++;
        if (q_xor == 0) q_matches++;
    }

    // Corrélation = moyenne des deux canaux
    float i_corr = (float)i_matches / CHIPS_PER_BIT;
    float q_corr = (float)q_matches / CHIPS_PER_BIT;
    return (i_corr + q_corr) / 2.0f;
}

/**
 * Affiche un graphique ASCII de la corrélation
 */
void plot_correlation_graph(const float *correlations, int num_bits) {
    printf("\n╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║              CORRELATION BIT PAR BIT (300 bits)                   ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    printf("Legende: # = >80%%  = = 60-80%%  - = 40-60%%  . = 20-40%%  <space> = <20%%\n\n");

    // Affichage par groupes de 10 bits
    for (int row = 0; row < 30; row++) {
        printf("Bits %3d-%3d: ", row * 10, row * 10 + 9);

        for (int col = 0; col < 10; col++) {
            int bit = row * 10 + col;
            float corr = correlations[bit];

            char symbol;
            if (corr > 0.80f) symbol = '#';
            else if (corr > 0.60f) symbol = '=';
            else if (corr > 0.40f) symbol = '-';
            else if (corr > 0.20f) symbol = '.';
            else symbol = ' ';

            printf("%c", symbol);
        }

        // Affiche stats pour cette ligne
        float sum = 0;
        for (int col = 0; col < 10; col++) {
            sum += correlations[row * 10 + col];
        }
        float avg = sum / 10.0f;
        printf("  %.1f%%\n", avg * 100.0f);
    }

    printf("\n");

    // Statistiques par zone
    float preamble_sum = 0;
    for (int i = 0; i < 50; i++) preamble_sum += correlations[i];
    float preamble_avg = preamble_sum / 50.0f;

    float data_sum = 0;
    for (int i = 50; i < 300; i++) data_sum += correlations[i];
    float data_avg = data_sum / 250.0f;

    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║ Préambule (bits 0-49):   %.1f%%                                     \n", preamble_avg * 100.0f);
    printf("║ Données   (bits 50-299): %.1f%%                                     \n", data_avg * 100.0f);
    printf("║ Transition bit 49→50:    %.1f%% → %.1f%% (Δ = %.1f%%)              \n",
           correlations[49] * 100.0f,
           correlations[50] * 100.0f,
           (correlations[50] - correlations[49]) * 100.0f);
    printf("╚════════════════════════════════════════════════════════════════════╝\n\n");

    // Détection du pattern de chute
    printf("DIAGNOSTIC:\n");
    float drop_49_50 = correlations[49] - correlations[50];
    if (drop_49_50 > 0.30f) {
        printf("⚠️  CHUTE BRUTALE au bit 50 (Δ=%.1f%%) → Problème de transition préambule/données\n",
               drop_49_50 * 100.0f);
    } else if (data_avg < preamble_avg - 0.10f) {
        printf("⚠️  DÉGRADATION PROGRESSIVE → Drift temporel ou désynchronisation PRN\n");
    } else if (preamble_avg < 0.60f) {
        printf("⚠️  CORRÉLATION FAIBLE SUR TOUT → Problème de paramètres globaux\n");
    } else {
        printf("✓  Pattern normal détecté\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test.iq> [chip_offset]\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int forced_chip_offset = (argc >= 3) ? atoi(argv[2]) : 0;

    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║         ANALYSE BIT-PAR-BIT DE LA CORRÉLATION DSSS               ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf("\nFichier: %s\n", filename);
    printf("Chip offset forcé: %+d chips\n\n", forced_chip_offset);

    // Charger le fichier IQ
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", filename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t num_samples = file_size / (2 * sizeof(float));
    printf("Échantillons chargés: %zu\n", num_samples);

    float *iq = malloc(file_size);
    fread(iq, 1, file_size, f);
    fclose(f);

    // Extraction des chips (downsampling à 38.4 kchips/s)
    const float SAMPLE_RATE = 2.5e6f;
    const float CHIP_RATE = 38400.0f;
    const float SAMPLES_PER_CHIP = SAMPLE_RATE / CHIP_RATE;

    int8_t *rx_i_chips = calloc(TOTAL_CHIPS + 100, sizeof(int8_t));
    int8_t *rx_q_chips = calloc(TOTAL_CHIPS + 100, sizeof(int8_t));

    for (int chip = 0; chip < TOTAL_CHIPS; chip++) {
        int sample_idx = (int)(chip * SAMPLES_PER_CHIP);
        if (sample_idx * 2 + 1 >= num_samples) break;

        float i = iq[sample_idx * 2];
        float q = iq[sample_idx * 2 + 1];

        rx_i_chips[chip] = (i > 0) ? 1 : -1;
        rx_q_chips[chip] = (q > 0) ? 1 : -1;
    }

    free(iq);

    // Générer PRN de référence
    printf("Génération PRN de référence...\n");
    prn_state_t prn_state;
    prn_init(&prn_state, 0);

    int8_t *prn_i = calloc(TOTAL_CHIPS, sizeof(int8_t));
    int8_t *prn_q = calloc(TOTAL_CHIPS, sizeof(int8_t));

    int8_t seq_i[256];
    int8_t seq_q[256];

    for (int bit = 0; bit < TOTAL_BITS; bit++) {
        // Générer 256 chips pour ce bit
        prn_generate_i(&prn_state, seq_i);
        prn_generate_q(&prn_state, seq_q);

        // Copier dans le buffer global
        memcpy(&prn_i[bit * CHIPS_PER_BIT], seq_i, CHIPS_PER_BIT);
        memcpy(&prn_q[bit * CHIPS_PER_BIT], seq_q, CHIPS_PER_BIT);
    }

    // Calculer corrélation pour chaque bit
    printf("Calcul de la corrélation bit par bit...\n\n");
    float correlations[TOTAL_BITS];

    for (int bit = 0; bit < TOTAL_BITS; bit++) {
        correlations[bit] = correlate_single_bit(rx_i_chips, rx_q_chips,
                                                 prn_i, prn_q,
                                                 bit, forced_chip_offset);
    }

    // Afficher le graphique
    plot_correlation_graph(correlations, TOTAL_BITS);

    // Export CSV pour gnuplot
    FILE *csv = fopen("correlation_per_bit.csv", "w");
    fprintf(csv, "bit,correlation\n");
    for (int i = 0; i < TOTAL_BITS; i++) {
        fprintf(csv, "%d,%.4f\n", i, correlations[i]);
    }
    fclose(csv);
    printf("Données exportées: correlation_per_bit.csv\n");

    // Cleanup
    free(rx_i_chips);
    free(rx_q_chips);
    free(prn_i);
    free(prn_q);

    return 0;
}
