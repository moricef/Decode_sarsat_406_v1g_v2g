#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Fonction pour convertir les bits en hexadécimal
void bits_to_hex(const uint8_t *bits, int num_bits, char *hex_output) {
    int hex_chars = (num_bits + 3) / 4;
    
    for (int i = 0; i < hex_chars; i++) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4 && (i * 4 + j) < num_bits; j++) {
            nibble = (nibble << 1) | bits[i * 4 + j];
        }
        sprintf(hex_output + i, "%X", nibble);
    }
    hex_output[hex_chars] = '\0';
}

// Fonction pour encoder un entier sur n bits (MSB first, indexé à partir de 0)
void encode_value(uint8_t *bits, int start_bit, int num_bits, uint32_t value) {
    for (int i = 0; i < num_bits; i++) {
        bits[start_bit + i] = (value >> (num_bits - 1 - i)) & 1;
    }
}

// Calculateur CRC-16 pour BCH simplifié
uint16_t calculate_crc16(const uint8_t *data, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

int main() {
    printf("=== GENERATEUR DE TRAMES 2G CORRIGEES ===\n\n");
    
    uint8_t frame[250] = {0};
    char hex_output[64];
    
    // === TRAME SIMPLE DE TEST ===
    printf("Trame 2G simplifiée pour validation :\n");
    memset(frame, 0, 250);
    
    // TAC Number: 12345 (0x3039) - bits 1-16
    encode_value(frame, 0, 16, 12345);
    
    // Serial Number: 1234 (0x04D2) - bits 17-30  
    encode_value(frame, 16, 14, 1234);
    
    // Country Code: 257 (France) - bits 31-40
    encode_value(frame, 30, 10, 257);
    
    // Status bits 41-43
    frame[40] = 1;  // Homing active
    frame[41] = 1;  // RLS enabled
    frame[42] = 0;  // Normal operation
    
    // Position par défaut (no capability) - bits 44-90
    // Latitude: 0 1111111 000001111100000
    frame[43] = 0;  // N/S flag  
    encode_value(frame, 44, 7, 0x7F);     // Degrees = 127
    encode_value(frame, 51, 15, 0x07C0);  // Fraction = 000001111100000
    
    // Longitude: 1 11111111 111110000011111  
    frame[66] = 1;  // E/W flag
    encode_value(frame, 67, 8, 0xFF);     // Degrees = 255
    encode_value(frame, 75, 15, 0x783F);  // Fraction = 111110000011111
    
    // Vessel ID Type: 001 (MMSI) - bits 91-93
    encode_value(frame, 90, 3, 1);
    
    // MMSI: 227000001 (France test) - bits 94-123
    encode_value(frame, 93, 30, 227000001);
    
    // AIS suffix: default - bits 124-137
    encode_value(frame, 123, 14, 0x2AAA);  // 10101010101010
    
    // Beacon Type: 001 (EPIRB) - bits 138-140
    encode_value(frame, 137, 3, 1);
    
    // Spare bits: all 1's - bits 141-154
    for (int i = 140; i < 154; i++) frame[i] = 1;
    
    // Rotating Field #0 - bits 155-202
    encode_value(frame, 154, 4, 0);   // Field ID = 0000
    encode_value(frame, 158, 6, 5);   // 5 hours elapsed
    encode_value(frame, 164, 11, 30); // 30 minutes since position
    encode_value(frame, 175, 10, 25); // Sea level (25 = (0+400)/16)
    encode_value(frame, 185, 4, 1);   // HDOP = 1-2
    encode_value(frame, 189, 4, 2);   // VDOP = 2-3
    encode_value(frame, 193, 2, 0);   // Manual activation
    encode_value(frame, 195, 3, 4);   // 50-75% battery
    encode_value(frame, 198, 2, 2);   // 3D fix
    encode_value(frame, 200, 2, 0);   // Spare = 00
    
    // BCH simplifié - bits 203-250
    for (int i = 202; i < 250; i++) {
        frame[i] = 0;
    }
    
    bits_to_hex(frame, 250, hex_output);
    printf("Hex: %s\n", hex_output);
    printf("Longueur: %lu caractères\n", strlen(hex_output));
    
    // Vérification des champs principaux
    printf("\n=== VERIFICATION ===\n");
    
    // Reconstituer les valeurs pour vérification
    uint32_t tac_check = 0, serial_check = 0, country_check = 0;
    for (int i = 0; i < 16; i++) tac_check = (tac_check << 1) | frame[i];
    for (int i = 0; i < 14; i++) serial_check = (serial_check << 1) | frame[16 + i];  
    for (int i = 0; i < 10; i++) country_check = (country_check << 1) | frame[30 + i];
    
    printf("TAC reconstruit: %u (attendu: 12345)\n", tac_check);
    printf("Serial reconstruit: %u (attendu: 1234)\n", serial_check);
    printf("Country reconstruit: %u (attendu: 257)\n", country_check);
    
    // Position par défaut
    uint32_t lat_deg = 0, lat_frac = 0, lon_deg = 0, lon_frac = 0;
    for (int i = 0; i < 7; i++) lat_deg = (lat_deg << 1) | frame[44 + i];
    for (int i = 0; i < 15; i++) lat_frac = (lat_frac << 1) | frame[51 + i];
    for (int i = 0; i < 8; i++) lon_deg = (lon_deg << 1) | frame[67 + i];
    for (int i = 0; i < 15; i++) lon_frac = (lon_frac << 1) | frame[75 + i];
    
    printf("Latitude: N/S=%d, deg=%u, frac=0x%X\n", frame[43], lat_deg, lat_frac);
    printf("Longitude: E/W=%d, deg=%u, frac=0x%X\n", frame[66], lon_deg, lon_frac);
    
    printf("\nCette trame devrait être décodée comme:\n");
    printf("- EPIRB française (pays 257)\n");
    printf("- TAC 12345, Serial 1234\n"); 
    printf("- MMSI 227000001\n");
    printf("- Position par défaut (pas de GNSS)\n");
    printf("- RF#0: 5h activation, 30min depuis position, niveau mer\n");
    
    return 0;
}