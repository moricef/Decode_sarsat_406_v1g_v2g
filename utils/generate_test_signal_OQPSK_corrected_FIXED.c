// test_signal_OQPSK_corrected_FIXED.c
#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "prn_generator.h"

#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f
#define SPREADING_FACTOR 256
#define NUM_BITS 300
#define NUM_BITS_PER_CHANNEL 150
#define PREAMBLE_LENGTH 50  // 25 I + 25 Q

int main() {
    // Calculs
    float sps = SAMPLE_RATE / CHIP_RATE;
    int samples_per_chip = (int)(sps + 0.5f);
    int q_delay_samples = (int)(sps / 2.0f + 0.5f);
    
    int total_chips_per_channel = NUM_BITS_PER_CHANNEL * SPREADING_FACTOR;
    int total_samples = total_chips_per_channel * samples_per_chip + q_delay_samples;
    
    printf("Generating CORRECT OQPSK signal with proper preamble...\n");
    printf("Samples: %d, Delay: %d samples\n", total_samples, q_delay_samples);
    
    // Allouer buffer
    float complex *signal = calloc(total_samples, sizeof(float complex));
    
    // ✅ CORRIGÉ : Préambule DSSS correct + données fixes
    uint8_t bits_I[NUM_BITS_PER_CHANNEL], bits_Q[NUM_BITS_PER_CHANNEL];
    
    // PRÉAMBULE : 50 bits à 0 (T.018 §2.2.4)
    for (int i = 0; i < PREAMBLE_LENGTH/2; i++) {
        bits_I[i] = 0;  // 25 bits I à 0
        bits_Q[i] = 0;  // 25 bits Q à 0
    }
    
    // DONNÉES : Pattern fixe (pas aléatoire) pour reproductibilité
    for (int i = PREAMBLE_LENGTH/2; i < NUM_BITS_PER_CHANNEL; i++) {
        bits_I[i] = (i % 2);      // 0,1,0,1...
        bits_Q[i] = ((i + 1) % 2); // 1,0,1,0...
    }
    
    // Sauvegarder les bits de référence
    FILE *f_ref = fopen("expected_bits_FIXED.bin", "wb");
    fwrite(bits_I, 1, NUM_BITS_PER_CHANNEL, f_ref);
    fwrite(bits_Q, 1, NUM_BITS_PER_CHANNEL, f_ref);
    fclose(f_ref);
    
    printf("Preamble: first 25 I-bits = ");
    for (int i = 0; i < 25; i++) printf("%d", bits_I[i]);
    printf("\nPreamble: first 25 Q-bits = ");
    for (int i = 0; i < 25; i++) printf("%d", bits_Q[i]);
    printf("\n");
    
    // Générer canal I
    prn_state_t prn_i;
    prn_init(&prn_i, 0);
    int8_t prn_seq_i[SPREADING_FACTOR];
    
    for (int bit = 0; bit < NUM_BITS_PER_CHANNEL; bit++) {
        prn_generate_i(&prn_i, prn_seq_i);
        
        for (int chip = 0; chip < SPREADING_FACTOR; chip++) {
            float chip_val = (bits_I[bit] == 0) ? (float)prn_seq_i[chip] : -(float)prn_seq_i[chip];
            
            int start_sample = (bit * SPREADING_FACTOR + chip) * samples_per_chip;
            
            for (int s = 0; s < samples_per_chip; s++) {
                if (start_sample + s < total_samples) {
                    signal[start_sample + s] = chip_val + I * cimag(signal[start_sample + s]);
                }
            }
        }
    }
    
    // Générer canal Q avec délai
    prn_state_t prn_q;
    prn_init(&prn_q, 0);
    int8_t prn_seq_q[SPREADING_FACTOR];
    
    for (int bit = 0; bit < NUM_BITS_PER_CHANNEL; bit++) {
        prn_generate_q(&prn_q, prn_seq_q);
        
        for (int chip = 0; chip < SPREADING_FACTOR; chip++) {
            float chip_val = (bits_Q[bit] == 0) ? (float)prn_seq_q[chip] : -(float)prn_seq_q[chip];
            
            int start_sample = (bit * SPREADING_FACTOR + chip) * samples_per_chip - q_delay_samples;
            
            for (int s = 0; s < samples_per_chip; s++) {
                int sample_idx = start_sample + s;
                if (sample_idx >= 0 && sample_idx < total_samples) {
                    signal[sample_idx] = creal(signal[sample_idx]) + I * chip_val;
                }
            }
        }
    }
    
    // Remplir le début (où Q n'est pas défini)
    for (int i = 0; i < q_delay_samples; i++) {
        signal[i] = creal(signal[i]) + I * 0.0f;
    }
    
    // Sauvegarder
    FILE *f = fopen("test_signal_CORRECT_FIXED.iq", "wb");
    fwrite(signal, sizeof(float complex), total_samples, f);
    fclose(f);
    
    printf("Correct signal with proper preamble saved to test_signal_CORRECT_FIXED.iq\n");
    printf("Expected bits saved to expected_bits_FIXED.bin\n");
    
    free(signal);
    return 0;
}
