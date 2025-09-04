/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


// dec406_main.c - Standalone hex decoder
#include "dec406.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void hex_string_to_bits(const char* hex_str, uint8_t* bits, int bit_count) {
    int hex_len = strlen(hex_str);
    int data_bits = hex_len * 4;
    
    // Check if we need to add sync pattern (15 bits of '1' + 9 bits frame sync)
    int needs_sync = 0;
    int sync_offset = 0;
    
    if (bit_count == FRAME_1G_SHORT || bit_count == FRAME_1G_LONG) {
        // For 1G frames, check if hex corresponds to data without sync
        if ((bit_count == FRAME_1G_SHORT && data_bits == (FRAME_1G_SHORT - 24)) ||  // 112-24=88 bits
            (bit_count == FRAME_1G_LONG && data_bits == (FRAME_1G_LONG - 24))) {   // 144-24=120 bits
            needs_sync = 1;
            sync_offset = 24; // 15 bits sync + 9 bits frame sync
            printf("Detected hex data without sync pattern - adding sync bits\n");
        }
    }
    
    if (needs_sync) {
        // Clear the entire bit array
        memset(bits, 0, bit_count * sizeof(uint8_t));
        
        // Add 15 sync bits (all '1')
        for (int i = 0; i < 15; i++) {
            bits[i] = 1;
        }
        
        // Add frame sync pattern (9 bits: 000101101)
        bits[15] = 0; bits[16] = 0; bits[17] = 0;
        bits[18] = 1; bits[19] = 0; bits[20] = 1;
        bits[21] = 1; bits[22] = 0; bits[23] = 1;
        
        // Convert hex data starting at bit 24
        for (int i = 0; i < hex_len; i++) {
            char c = tolower(hex_str[i]);
            int value;
            if (c >= 'a' && c <= 'f') {
                value = c - 'a' + 10;
            } else if (c >= '0' && c <= '9') {
                value = c - '0';
            } else {
                value = 0;
                fprintf(stderr, "Avertissement: caractère non hexadécimal '%c' à la position %d\n", c, i);
            }
            
            int bit_pos = 24 + i * 4;
            if (bit_pos + 3 < bit_count) {
                bits[bit_pos]   = (value >> 3) & 1;
                bits[bit_pos+1] = (value >> 2) & 1;
                bits[bit_pos+2] = (value >> 1) & 1;
                bits[bit_pos+3] = (value >> 0) & 1;
            }
        }
    } else {
        // Original behavior for complete frames
        if (data_bits < bit_count) {
            fprintf(stderr, "Chaîne hexadécimale trop courte: %d bits < %d bits\n", data_bits, bit_count);
            exit(EXIT_FAILURE);
        }

        // Special handling for 2G frames with padding
        if (bit_count == FRAME_2G_LENGTH) {
            // For 2G: expect 63 hex chars = 252 bits = 2 padding + 250 data
            // Skip the first 2 padding bits
            int bit_offset = 0;
            
            for (int i = 0; i < hex_len; i++) {
                char c = tolower(hex_str[i]);
                int value;
                if (c >= 'a' && c <= 'f') {
                    value = c - 'a' + 10;
                } else if (c >= '0' && c <= '9') {
                    value = c - '0';
                } else {
                    value = 0;
                    fprintf(stderr, "Avertissement: caractère non hexadécimal '%c' à la position %d\n", c, i);
                }
                
                // Extract 4 bits from this hex digit
                for (int j = 3; j >= 0; j--) {
                    int bit_pos = i * 4 + (3 - j);
                    int bit_val = (value >> j) & 1;
                    
                    // Skip first 2 padding bits, then store the next 250 bits
                    if (bit_pos >= 2 && bit_offset < bit_count) {
                        bits[bit_offset++] = bit_val;
                    }
                }
            }
            printf("2G frame detected: skipped 2 padding bits, extracted %d data bits\n", bit_offset);
        } else {
            // Original 1G handling
            for (int i = 0; i < hex_len; i++) {
                char c = tolower(hex_str[i]);
                int value;
                if (c >= 'a' && c <= 'f') {
                    value = c - 'a' + 10;
                } else if (c >= '0' && c <= '9') {
                    value = c - '0';
                } else {
                    value = 0;
                    fprintf(stderr, "Avertissement: caractère non hexadécimal '%c' à la position %d\n", c, i);
                }
                
                if (i*4 + 3 < bit_count) {
                    bits[i*4]   = (value >> 3) & 1;
                    bits[i*4+1] = (value >> 2) & 1;
                    bits[i*4+2] = (value >> 1) & 1;
                    bits[i*4+3] = (value >> 0) & 1;
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <chaîne_hexadécimale>\n", argv[0]);
        fprintf(stderr, "Formats supportés :\n");
        fprintf(stderr, " - 28 caractères (112 bits) : Trame 1G courte\n");
        fprintf(stderr, " - 36 caractères (144 bits) : Trame 1G longue\n");
        fprintf(stderr, " - 63-64 caractères (250 bits) : Trame 2G\n");
        return EXIT_FAILURE;
    }

    char* hex_str = argv[1];
    int clean_len = 0;
    char cleaned_hex[256] = {0};

    // Nettoyer la chaîne
    for (size_t i = 0; i < strlen(hex_str); i++) {
        if (isxdigit(hex_str[i])) {
            cleaned_hex[clean_len++] = tolower(hex_str[i]);
        }
    }
    cleaned_hex[clean_len] = '\0';

    int bit_count;
    if (clean_len == 28) {
        bit_count = FRAME_1G_SHORT;  // 112 bits
    } else if (clean_len == 36) {
        bit_count = FRAME_1G_LONG;   // 144 bits
    } else if (clean_len == 63 || clean_len == 64) {
        bit_count = FRAME_2G_LENGTH; // 250 bits (63 ou 64 caractères hex)
    } else if (clean_len == 22) {
        // 22 hex chars = 88 bits = 112-24 bits (1G short sans sync)
        bit_count = FRAME_1G_SHORT;  
    } else if (clean_len == 30) {
        // 30 hex chars = 120 bits = 144-24 bits (1G long sans sync) 
        bit_count = FRAME_1G_LONG;   
    } else {
        fprintf(stderr, "Longueur hexadécimale non supportée : %d\n", clean_len);
        fprintf(stderr, "Formats attendus :\n");
        fprintf(stderr, " - 22 caractères pour trames 1G courtes sans sync (88 bits de données)\n");
        fprintf(stderr, " - 28 caractères pour trames 1G courtes complètes (112 bits)\n");
        fprintf(stderr, " - 30 caractères pour trames 1G longues sans sync (120 bits de données)\n");
        fprintf(stderr, " - 36 caractères pour trames 1G longues complètes (144 bits)\n");
        fprintf(stderr, " - 63-64 caractères pour trames 2G (250 bits)\n");
        return EXIT_FAILURE;
    }

    uint8_t* bits = malloc(bit_count * sizeof(uint8_t));
    if (!bits) {
        perror("Erreur d'allocation mémoire");
        return EXIT_FAILURE;
    }

    memset(bits, 0, bit_count * sizeof(uint8_t));
    hex_string_to_bits(cleaned_hex, bits, bit_count);
    decode_beacon(bits, bit_count);
    free(bits);

    return EXIT_SUCCESS;
}
