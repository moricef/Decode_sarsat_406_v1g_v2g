#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "prn_generator.h"

#define SAMPLE_RATE 2500000.0f
#define CHIP_RATE 38400.0f
#define SPREADING_FACTOR 256  // 256 chips/bit par voie (I et Q séparément)
#define NUM_BITS 300
#define NUM_SYMBOLS (NUM_BITS / 2)  // 150 symboles OQPSK (I+Q)

int main() {
    // Calculer les paramètres
    float sps = SAMPLE_RATE / CHIP_RATE;  // ~65.1 samples/chip
    int samples_per_chip = (int)(sps + 0.5f);
    int q_delay_samples = (int)(sps / 2.0f + 0.5f);  // Délai OQPSK
    
    int total_chips = NUM_BITS * SPREADING_FACTOR;  // 300 * 256 = 76,800 chips
    int total_samples = total_chips * samples_per_chip;
    
    printf("Generating test DSSS/OQPSK signal...\n");
    printf("Bits: %d, Symbols: %d, Chips: %d, Samples: %d\n", 
           NUM_BITS, NUM_SYMBOLS, total_chips, total_samples);
    printf("Chip Rate: %.0f chips/sec, Data Rate: %d bps\n", 
           CHIP_RATE, (int)(CHIP_RATE / (SPREADING_FACTOR/2)));  // 300 bps
    printf("Spreading: %d chips/bit per voie (I/Q)\n", SPREADING_FACTOR);
    printf("OQPSK delay: %d samples\n", q_delay_samples);
    
    // Allouer les buffers
    float complex *signal = malloc(total_samples * sizeof(float complex));
    
    // Générer les bits de test
    uint8_t test_bits[NUM_BITS];
    for (int i = 0; i < NUM_BITS; i++) {
        test_bits[i] = (i < 50) ? 0 : ((i * 13 + 7) % 2);  // Préambule 50x0 + données
    }
    
    // Générer le signal DSSS/OQPSK CORRECT
    prn_state_t prn_i, prn_q;
    prn_init(&prn_i, 0);
    prn_init(&prn_q, 0);
    
    int8_t prn_seq_i[SPREADING_FACTOR];
    int8_t prn_seq_q[SPREADING_FACTOR];
    
    // Buffers séparés pour I et Q
    float *i_samples = calloc(total_samples + q_delay_samples, sizeof(float));
    float *q_samples = calloc(total_samples + q_delay_samples, sizeof(float));
    
    int sample_idx = 0;
    
    // OQPSK : 2 bits par symbole (I et Q)
    for (int symbol = 0; symbol < NUM_SYMBOLS; symbol++) {
        uint8_t bit_i = test_bits[2 * symbol];      // Bit I
        uint8_t bit_q = test_bits[2 * symbol + 1];  // Bit Q
        
        prn_generate_i(&prn_i, prn_seq_i);
        prn_generate_q(&prn_q, prn_seq_q);
        
        // Générer chips I et Q
        for (int chip = 0; chip < SPREADING_FACTOR; chip++) {
            float chip_i = (bit_i == 0) ? (float)prn_seq_i[chip] : -(float)prn_seq_i[chip];
            float chip_q = (bit_q == 0) ? (float)prn_seq_q[chip] : -(float)prn_seq_q[chip];
            
            // Remplir buffers I et Q séparément
            for (int s = 0; s < samples_per_chip; s++) {
                i_samples[sample_idx] = chip_i;
                q_samples[sample_idx + q_delay_samples] = chip_q;  // Q décalé
                sample_idx++;
            }
        }
    }
    
    // Combiner I et Q avec délai OQPSK
    for (int i = 0; i < total_samples; i++) {
        signal[i] = i_samples[i] + I * q_samples[i];
    }
    
    // Ajouter du bruit réaliste
    srand(42);
    for (int i = 0; i < total_samples; i++) {
        float noise_i = ((float)rand()/RAND_MAX - 0.5f) * 0.05f;  // ±2.5% bruit
        float noise_q = ((float)rand()/RAND_MAX - 0.5f) * 0.05f;
        signal[i] = creal(signal[i]) + noise_i + I * (cimag(signal[i]) + noise_q);
    }
    
    // Sauvegarder
    FILE *f = fopen("test_signal_OQPSK.iq", "wb");
    fwrite(signal, sizeof(float complex), total_samples, f);
    fclose(f);
    
    printf("Signal saved to test_signal_OQPSK.iq\n");
    printf("Verification: I[0]=%.3f, Q[0]=%.3f, Q[%d]=%.3f\n", 
           i_samples[0], q_samples[0], q_delay_samples, q_samples[q_delay_samples]);
    
    free(signal);
    free(i_samples);
    free(q_samples);
    return 0;
}
