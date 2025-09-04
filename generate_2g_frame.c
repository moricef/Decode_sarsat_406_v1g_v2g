#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Fonction pour convertir les bits en hexadécimal avec padding à 64 caractères
void bits_to_hex_padded(const uint8_t *bits, char *hex_output) {
    // Ajouter 2 bits de padding au début (00 pour mode normal)
    uint8_t padded_bits[252] = {0, 0}; // Start with 00
    memcpy(padded_bits + 2, bits, 250); // Copy our 250 bits
    
    for (int i = 0; i < 63; i++) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4; j++) {
            nibble = (nibble << 1) | padded_bits[i * 4 + j];
        }
        sprintf(hex_output + i, "%X", nibble);
    }
    hex_output[63] = '\0';
}

// Encoder en respectant exactement l'ordre des bits T.018
void encode_value(uint8_t *bits, int start_bit, int num_bits, uint32_t value) {
    for (int i = 0; i < num_bits; i++) {
        bits[start_bit + i] = (value >> (num_bits - 1 - i)) & 1;
    }
}

int main() {
    printf("=== GENERATEUR TRAME 2G ===\n\n");
    
    uint8_t frame[250] = {0};
    char hex_output[65];
    
    printf("Génération d'une trame 2G conforme T.018...\n");
    
    // === EPIRB MARITIME SIMPLE ===
    // TAC Number: 12345 (bits 1-16 dans le message)
    // Conversion: 12345 = 0011000000111001 en binaire
    encode_value(frame, 0, 16, 12345);
    
    // Serial Number: 1234 (bits 17-30 dans le message) 
    // Conversion: 1234 = 10011010010 en 14 bits = 00010011010010
    encode_value(frame, 16, 14, 1234);
    
    // Country Code: 257 (bits 31-40 dans le message)
    // Conversion: 257 = 0100000001 en 10 bits
    encode_value(frame, 30, 10, 257);
    
    // Status bits (bits 41-43)
    frame[40] = 1;  // Homing device active
    frame[41] = 1;  // RLS enabled
    frame[42] = 0;  // Normal operation (not test)
    
    // Position réelle simple: Paris (48.8566°N, 2.3522°E) 
    // Latitude: 48.8566°N
    int lat_deg = 48;
    int lat_frac = (int)((48.8566 - 48.0) * 32768.0); // 0.8566 * 32768 = 28074
    frame[43] = 0;  // North
    encode_value(frame, 44, 7, lat_deg);
    encode_value(frame, 51, 15, lat_frac);
    
    // Longitude: 2.3522°E  
    int lon_deg = 2;
    int lon_frac = (int)((2.3522 - 2.0) * 32768.0); // 0.3522 * 32768 = 11540
    frame[66] = 0;  // East
    encode_value(frame, 67, 8, lon_deg);
    encode_value(frame, 75, 15, lon_frac);
    
    // Vessel ID Type: 001 (MMSI) - bits 91-93
    encode_value(frame, 90, 3, 1);
    
    // MMSI: 227123456 (France, format correct) - bits 94-123
    encode_value(frame, 93, 30, 227123456);
    
    // AIS suffix par défaut - bits 124-137  
    encode_value(frame, 123, 14, 0x2AAA); // 10101010101010
    
    // Beacon Type: 001 (EPIRB) - bits 138-140
    encode_value(frame, 137, 3, 1);
    
    // Spare bits: tous à 1 (bits 141-154)
    for (int i = 140; i < 154; i++) {
        frame[i] = 1;
    }
    
    // Rotating Field #0 (bits 155-202)
    encode_value(frame, 154, 4, 0);   // Field ID
    encode_value(frame, 158, 6, 8);   // 8 hours elapsed
    encode_value(frame, 164, 11, 15); // 15 minutes since last position
    encode_value(frame, 175, 10, 62); // 600m altitude: (600+400)/16 = 62.5 ≈ 62
    encode_value(frame, 185, 4, 2);   // HDOP = 2-3
    encode_value(frame, 189, 4, 1);   // VDOP = 1-2  
    encode_value(frame, 193, 2, 0);   // Manual activation
    encode_value(frame, 195, 3, 5);   // 75-100% battery
    encode_value(frame, 198, 2, 2);   // 3D fix
    encode_value(frame, 200, 2, 0);   // Spare = 00
    
    // BCH dummy (bits 203-250)
    for (int i = 202; i < 250; i++) {
        frame[i] = 0;
    }
    
    bits_to_hex_padded(frame, hex_output);
    printf("Hex: %s\n", hex_output);
    printf("Longueur: %lu caractères\n", strlen(hex_output));
    
    // Vérifications
    printf("\n=== VERIFICATION MANUELLE ===\n");
    printf("TAC (bits 1-16): ");
    for (int i = 0; i < 16; i++) printf("%d", frame[i]);
    printf(" = %d\n", 12345);
    
    printf("Serial (bits 17-30): ");
    for (int i = 16; i < 30; i++) printf("%d", frame[i]);
    printf(" = %d\n", 1234);
    
    printf("Country (bits 31-40): ");
    for (int i = 30; i < 40; i++) printf("%d", frame[i]);
    printf(" = %d\n", 257);
    
    printf("\nPosition: Paris (48.8566°N, 2.3522°E)\n");
    printf("MMSI: 227123456 (France)\n");
    printf("Type: EPIRB (001)\n");
    printf("RF#0: 8h elapsed, 15min depuis position, 600m altitude\n");
    
    return 0;
}