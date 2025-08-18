
// dec406_main.c
#include "dec406.h"
#include "audio_capture.h"
#include <stdio.h>   // Ajout pour fprintf()
#include <stdlib.h>  // Ajout pour EXIT_SUCCESS

void decode_beacon(const uint8_t *bits, int length) {
    if (!bits) {
        fprintf(stderr, "Erreur: bits est NULL\n");
        return;
    }
    
    switch(length) {
        case FRAME_1G_LENGTH:
            printf("Démarrage du décodage 1G...\n");
            decode_1g(bits, length);
            break;
            
        case FRAME_2G_LENGTH:
            printf("Démarrage du décodage 2G...\n");
            decode_2g(bits);
            break;
            
        default:
            fprintf(stderr, "Erreur: Longueur de trame %d non supportée\n", length);
    }
}

int main() {
    const char* audio_file = "beacon_capture.wav";
    
     capture_audio(audio_file, 30);
     
     uint8_t* bits = NULL;
    if (detect_beacon_signal(audio_file, &bits)) {
        printf("Signal balise 406 MHz détecté!\n");
        
        
        } else {
        printf("Aucun signal balise détecté.\n");
    }
    
    // Données de test pour une balise 1G (112 bits)
 uint8_t bits_1g[FRAME_1G_LENGTH] = {
        1,0,1,1,0,0,1,0,1,1,0,0,1,0,1,0,0,1,1,0,0,1,0,1        
};

    // Données de test pour une balise 2G (250 bits)
    uint8_t bits_2g[FRAME_2G_LENGTH] = {0}; // Tous les bits à zéro
    
    // Initialisation manuelle des premiers bits pour le test
    bits_2g[0] = 1; bits_2g[2] = 1; bits_2g[4] = 1; bits_2g[7] = 1;
    bits_2g[9] = 1; bits_2g[11] = 1; bits_2g[14] = 1; bits_2g[16] = 1;
    bits_2g[18] = 1; bits_2g[22] = 1; bits_2g[24] = 1; bits_2g[26] = 1;
    bits_2g[29] = 1;

    printf("=== Début du décodage des balises 406 MHz ===\n");
    
    // Décoder une balise 1G
    decode_beacon(bits_1g, FRAME_1G_LENGTH);
    
    // Décoder une balise 2G
    decode_beacon(bits_2g, FRAME_2G_LENGTH);
    
    printf("=== Décodage terminé avec succès ===\n");
    return EXIT_SUCCESS;
}
