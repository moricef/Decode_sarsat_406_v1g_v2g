/**
 * @file test_timing_oqpsk.c
 * @brief Test de la fonction dsss_timing_recovery_oqpsk()
 *
 * Compare avec la fonction existante dsss_timing_recovery_corrected()
 * pour valider que la nouvelle approche MATLAB-compatible fonctionne
 */

#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "dsss_demod.h"

#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f

int main() {
    printf("=================================================================\n");
    printf("TEST: OQPSK Timing Recovery (MATLAB-compatible)\n");
    printf("=================================================================\n\n");

    // Charger le signal de test
    FILE *f = fopen("test_signal_CORRECT_FIXED.iq", "rb");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir test_signal_CORRECT_FIXED.iq\n");
        fprintf(stderr, "Générer d'abord avec: gcc generate_test_signal_OQPSK_corrected_FIXED.c prn_generator.c -o generate_test_signal -lm && ./generate_test_signal\n");
        return 1;
    }

    // Déterminer la taille
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t num_samples = file_size / sizeof(float complex);
    printf("Signal test: %zu échantillons (%.3f secondes @ %.1f kHz)\n",
           num_samples, num_samples / SAMPLE_RATE, SAMPLE_RATE / 1000.0f);

    float complex *signal = malloc(num_samples * sizeof(float complex));
    if (!signal) {
        fclose(f);
        fprintf(stderr, "Erreur allocation mémoire\n");
        return 1;
    }

    size_t read_count = fread(signal, sizeof(float complex), num_samples, f);
    fclose(f);

    if (read_count != num_samples) {
        fprintf(stderr, "Erreur lecture fichier\n");
        free(signal);
        return 1;
    }

    printf("\n--- Configuration ---\n");
    printf("Sample rate: %.1f kHz\n", SAMPLE_RATE / 1000.0f);
    printf("Chip rate: %.1f kHz\n", CHIP_RATE / 1000.0f);
    printf("Samples per chip: %.3f\n", SAMPLE_RATE / CHIP_RATE);
    printf("Chips attendus: 38400 (150 bits × 256 spreading)\n");

    // Allouer buffer pour symboles de sortie
    float complex *symbols_oqpsk = calloc(40000, sizeof(float complex));
    if (!symbols_oqpsk) {
        free(signal);
        fprintf(stderr, "Erreur allocation symboles\n");
        return 1;
    }

    // =========================================================================
    // TEST: dsss_timing_recovery_oqpsk()
    // =========================================================================
    printf("\n=================================================================\n");
    printf("TEST 1: dsss_timing_recovery_oqpsk() - NOUVEAU (MATLAB style)\n");
    printf("=================================================================\n\n");

    size_t num_symbols_oqpsk = 0;
    int result = dsss_timing_recovery_oqpsk(signal, symbols_oqpsk,
                                            num_samples, &num_symbols_oqpsk,
                                            SAMPLE_RATE);

    printf("\n--- Résultats ---\n");
    printf("Retour: %d (%s)\n", result, (result == 0) ? "SUCCÈS" : "ÉCHEC");
    printf("Symboles récupérés: %zu / 38400 (%.1f%%)\n",
           num_symbols_oqpsk, 100.0f * num_symbols_oqpsk / 38400.0f);

    if (num_symbols_oqpsk > 0) {
        printf("\nPremiers 10 symboles QPSK:\n");
        for (int i = 0; i < 10 && i < (int)num_symbols_oqpsk; i++) {
            printf("  [%d] %.4f + j%.4f  (mag=%.4f, phase=%.1f°)\n",
                   i, crealf(symbols_oqpsk[i]), cimagf(symbols_oqpsk[i]),
                   cabsf(symbols_oqpsk[i]),
                   cargf(symbols_oqpsk[i]) * 180.0f / M_PI);
        }

        // Vérifier constellation QPSK (devrait avoir 4 points autour de ±1±j)
        printf("\nAnalyse constellation (1000 premiers symboles):\n");
        int quadrant[4] = {0, 0, 0, 0};  // Q1(+,+), Q2(-,+), Q3(-,-), Q4(+,-)
        float sum_mag = 0.0f;

        for (int i = 0; i < 1000 && i < (int)num_symbols_oqpsk; i++) {
            float re = crealf(symbols_oqpsk[i]);
            float im = cimagf(symbols_oqpsk[i]);
            sum_mag += cabsf(symbols_oqpsk[i]);

            if (re >= 0 && im >= 0) quadrant[0]++;
            else if (re < 0 && im >= 0) quadrant[1]++;
            else if (re < 0 && im < 0) quadrant[2]++;
            else quadrant[3]++;
        }

        printf("  Quadrants: Q1=%.1f%% Q2=%.1f%% Q3=%.1f%% Q4=%.1f%%\n",
               100.0f * quadrant[0] / 1000.0f,
               100.0f * quadrant[1] / 1000.0f,
               100.0f * quadrant[2] / 1000.0f,
               100.0f * quadrant[3] / 1000.0f);
        printf("  Magnitude moyenne: %.4f (attendu: ~1.0)\n", sum_mag / 1000.0f);
    }

    // =========================================================================
    // SAUVEGARDE pour analyse
    // =========================================================================
    if (num_symbols_oqpsk > 0) {
        FILE *f_out = fopen("symbols_oqpsk_recovered.iq", "wb");
        if (f_out) {
            fwrite(symbols_oqpsk, sizeof(float complex), num_symbols_oqpsk, f_out);
            fclose(f_out);
            printf("\nSymboles sauvegardés dans symbols_oqpsk_recovered.iq\n");
        }
    }

    // =========================================================================
    // CONCLUSION
    // =========================================================================
    printf("\n=================================================================\n");
    printf("CONCLUSION\n");
    printf("=================================================================\n");

    if (result == 0 && num_symbols_oqpsk >= 38000) {
        printf("✅ TEST RÉUSSI: Timing recovery OQPSK fonctionne!\n");
        printf("   - %zu symboles récupérés (>99%% de 38400)\n", num_symbols_oqpsk);
        printf("   - Prêt pour PHASE 2: Phase ambiguity resolution\n");
    } else if (result == 0) {
        printf("⚠️  TEST PARTIEL: Timing recovery fonctionne mais trop peu de symboles\n");
        printf("   - Seulement %zu / 38400 symboles (%.1f%%)\n",
               num_symbols_oqpsk, 100.0f * num_symbols_oqpsk / 38400.0f);
        printf("   - Vérifier paramètres loop bandwidth / gains\n");
    } else {
        printf("❌ TEST ÉCHEC: Timing recovery OQPSK a échoué\n");
        printf("   - Code retour: %d\n", result);
        printf("   - Vérifier logs ci-dessus pour détails\n");
    }

    free(signal);
    free(symbols_oqpsk);

    return (result == 0 && num_symbols_oqpsk >= 38000) ? 0 : 1;
}
