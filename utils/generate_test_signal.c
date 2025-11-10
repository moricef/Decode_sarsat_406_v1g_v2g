#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "prn_generator.h"

#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f
#define SPREADING_FACTOR 256
#define NUM_BITS 300

int main() {
    // Calculer les paramètres
    float sps = SAMPLE_RATE / CHIP_RATE;  // ~65.1 samples/chip
    int samples_per_chip = (int)(sps + 0.5f);
    int total_chips = NUM_BITS * SPREADING_FACTOR;
    int total_samples = total_chips * samples_per_chip;
    
    printf("Generating test DSSS signal...\n");
    printf("Bits: %d, Chips: %d, Samples: %d\n", NUM_BITS, total_chips, total_samples);
    
    // Allouer le buffer
    float complex *signal = malloc(total_samples * sizeof(float complex));
    
    // Générer les bits de test (preamble + données simples)
    uint8_t test_bits[NUM_BITS];
    for (int i = 0; i < NUM_BITS; i++) {
        // Preamble: alternance 0,1,0,1...
        if (i < 50) {
            test_bits[i] = (i % 2);
        } else {
            // Données simples
            test_bits[i] = (i / 10) % 2;
        }
    }
    
    // Générer le signal DSSS
    prn_state_t prn_i, prn_q;
    prn_init(&prn_i, 0);
    prn_init(&prn_q, 0);
    
    int sample_idx = 0;
    int8_t prn_seq_i[SPREADING_FACTOR];
    int8_t prn_seq_q[SPREADING_FACTOR];
    
    for (int bit = 0; bit < NUM_BITS / 2; bit++) {
        // Générer les séquences PRN pour ce bit
        prn_generate_i(&prn_i, prn_seq_i);
        prn_generate_q(&prn_q, prn_seq_q);
        
        // Bits I et Q (interleaved)
        int bit_i = test_bits[2 * bit];
        int bit_q = test_bits[2 * bit + 1];
        
        // Étaler chaque bit sur 256 chips
        for (int chip = 0; chip < SPREADING_FACTOR; chip++) {
            float chip_i = (bit_i == 0) ? (float)prn_seq_i[chip] : -(float)prn_seq_i[chip];
            float chip_q = (bit_q == 0) ? (float)prn_seq_q[chip] : -(float)prn_seq_q[chip];
            
            // Répéter chaque chip sur sps échantillons (zero-order hold)
            for (int s = 0; s < samples_per_chip; s++) {
                signal[sample_idx++] = chip_i + I * chip_q;
            }
        }
    }
    
    // Sauvegarder le fichier
    FILE *f = fopen("test_signal.iq", "wb");
    fwrite(signal, sizeof(float complex), total_samples, f);
    fclose(f);
    
    printf("Signal saved to test_signal.iq (%d samples)\n", total_samples);
    printf("File size: %ld bytes\n", total_samples * sizeof(float complex));
    
    free(signal);
    return 0;
}
