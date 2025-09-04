/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


// dec406.c - Main decoder dispatch function
#include "dec406.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Main beacon decoding dispatcher
 * Routes to appropriate decoder based on frame length
 */
void decode_beacon(const uint8_t *bits, int length) {
    if (!bits) {
        fprintf(stderr, "Error: NULL bits array\n");
        return;
    }
    
    switch(length) {
        case FRAME_1G_SHORT:
        case FRAME_1G_LONG:
            printf("Starting 1G decoding (%s frame)...\n", 
                  (length == FRAME_1G_SHORT) ? "short" : "long");
            decode_1g(bits, length);
            break;
            
        case FRAME_2G_LENGTH:
            printf("Starting 2G decoding...\n");
            decode_2g(bits);
            break;
            
        default:
            fprintf(stderr, "Error: Unsupported frame length: %d bits\n", length);
            fprintf(stderr, "Supported lengths:\n");
            fprintf(stderr, "  - %d bits: 1G short frame\n", FRAME_1G_SHORT);
            fprintf(stderr, "  - %d bits: 1G long frame\n", FRAME_1G_LONG);
            fprintf(stderr, "  - %d bits: 2G frame\n", FRAME_2G_LENGTH);
    }
}
