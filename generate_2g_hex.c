#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Fonction pour convertir les bits en hexadécimal
void bits_to_hex(const uint8_t *bits, int num_bits, char *hex_output) {
    int hex_chars = (num_bits + 3) / 4;  // Arrondir vers le haut
    
    for (int i = 0; i < hex_chars; i++) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4 && (i * 4 + j) < num_bits; j++) {
            nibble = (nibble << 1) | bits[i * 4 + j];
        }
        sprintf(hex_output + i, "%X", nibble);
    }
    hex_output[hex_chars] = '\0';
}

// Fonction pour encoder un entier sur n bits (MSB first)
void encode_value(uint8_t *bits, int start_bit, int num_bits, uint32_t value) {
    for (int i = 0; i < num_bits; i++) {
        bits[start_bit + i] = (value >> (num_bits - 1 - i)) & 1;
    }
    
    // Debug output for verification
    printf("DEBUG: Encoding %u (%d bits) at position %d: ", value, num_bits, start_bit);
    for (int i = 0; i < num_bits; i++) {
        printf("%d", bits[start_bit + i]);
    }
    printf("\n");
}

// Fonction pour encoder la position GNSS
void encode_position(uint8_t *bits, double lat, double lon) {
    // Latitude
    int ns_flag = (lat < 0) ? 1 : 0;
    lat = (lat < 0) ? -lat : lat;
    int lat_deg = (int)lat;
    int lat_frac = (int)((lat - lat_deg) * 32768.0);
    
    bits[43] = ns_flag;
    encode_value(bits, 44, 7, lat_deg);
    encode_value(bits, 51, 15, lat_frac);
    
    // Longitude  
    int ew_flag = (lon < 0) ? 1 : 0;
    lon = (lon < 0) ? -lon : lon;
    int lon_deg = (int)lon;
    int lon_frac = (int)((lon - lon_deg) * 32768.0);
    
    bits[66] = ew_flag;
    encode_value(bits, 67, 8, lon_deg);
    encode_value(bits, 75, 15, lon_frac);
}

// Fonction pour encoder un call sign en Modified Baudot
void encode_callsign(uint8_t *bits, int start_bit, const char *callsign) {
    // Table Modified Baudot simplifiée
    uint8_t baudot_map[256] = {0};
    baudot_map['A'] = 0b111000; baudot_map['B'] = 0b110011; baudot_map['C'] = 0b101110;
    baudot_map['D'] = 0b110010; baudot_map['E'] = 0b110000; baudot_map['F'] = 0b110110;
    baudot_map['G'] = 0b101011; baudot_map['H'] = 0b100101; baudot_map['I'] = 0b101100;
    baudot_map['J'] = 0b111010; baudot_map['K'] = 0b111110; baudot_map['L'] = 0b101001;
    baudot_map['M'] = 0b100111; baudot_map['N'] = 0b100110; baudot_map['O'] = 0b100011;
    baudot_map['P'] = 0b101101; baudot_map['Q'] = 0b111101; baudot_map['R'] = 0b101010;
    baudot_map['S'] = 0b110100; baudot_map['T'] = 0b100001; baudot_map['U'] = 0b111100;
    baudot_map['V'] = 0b101111; baudot_map['W'] = 0b111001; baudot_map['X'] = 0b110111;
    baudot_map['Y'] = 0b110101; baudot_map['Z'] = 0b110001; baudot_map[' '] = 0b100100;
    baudot_map['0'] = 0b001101; baudot_map['1'] = 0b011101; baudot_map['2'] = 0b011001;
    baudot_map['3'] = 0b010000; baudot_map['4'] = 0b001010; baudot_map['5'] = 0b000001;
    baudot_map['6'] = 0b010101; baudot_map['7'] = 0b011100; baudot_map['8'] = 0b001100;
    baudot_map['9'] = 0b000011; baudot_map['-'] = 0b011000; baudot_map['/'] = 0b010111;
    
    for (int i = 0; i < 7; i++) {
        uint8_t code = (i < strlen(callsign)) ? baudot_map[(uint8_t)callsign[i]] : baudot_map[' '];
        encode_value(bits, start_bit + i * 6, 6, code);
    }
}

int main() {
    printf("=== GENERATEUR DE TRAME 2G HEXADECIMALE ===\n\n");
    
    // Créer plusieurs exemples de trames 2G
    uint8_t frame[250] = {0};
    char hex_output[64];
    
    // === EXEMPLE 1: EPIRB Maritime française avec position ===
    printf("1. EPIRB Maritime française avec MMSI et position GPS:\n");
    memset(frame, 0, 250);
    
    // TAC Number: 15000
    encode_value(frame, 0, 16, 15000);
    
    // Serial Number: 5678
    encode_value(frame, 16, 14, 5678);
    
    // Country Code: 257 (France)
    encode_value(frame, 30, 10, 257);
    
    // Status bits
    frame[40] = 1;  // Homing active
    frame[41] = 1;  // RLS enabled  
    frame[42] = 0;  // Normal operation
    
    // Position réelle: Brest, France (48.390°N, 4.486°W)
    encode_position(frame, 48.390, -4.486);
    
    // Vessel ID Type: 001 (MMSI)
    encode_value(frame, 90, 3, 1);
    
    // MMSI: 227001234 (France)
    encode_value(frame, 93, 30, 227001234);
    
    // AIS suffix: 1234
    encode_value(frame, 123, 14, 1234);
    
    // Beacon Type: 001 (EPIRB)
    encode_value(frame, 137, 3, 1);
    
    // Spare bits: all 1's
    for (int i = 140; i < 154; i++) frame[i] = 1;
    
    // Rotating Field #0
    encode_value(frame, 154, 4, 0);  // Field ID
    encode_value(frame, 158, 6, 12); // 12 hours elapsed
    encode_value(frame, 164, 11, 5); // 5 minutes since last position
    encode_value(frame, 175, 10, (50 + 400) / 16); // 50m altitude
    encode_value(frame, 185, 8, 0x12); // HDOP=1, VDOP=2
    encode_value(frame, 193, 2, 0);  // Manual activation
    encode_value(frame, 195, 3, 4);  // 50-75% battery
    encode_value(frame, 198, 2, 2);  // 3D fix
    
    // BCH dummy (simplified)
    for (int i = 202; i < 250; i++) {
        frame[i] = (i % 7 < 3) ? 1 : 0;
    }
    
    bits_to_hex(frame, 250, hex_output);
    printf("Hex: %s\n", hex_output);
    printf("Longueur: %lu caractères (%d bits)\n\n", strlen(hex_output), 250);
    
    // === EXEMPLE 2: ELT(DT) avec call sign ===
    printf("2. ELT(DT) Avion avec Call Sign:\n");
    memset(frame, 0, 250);
    
    // TAC Number: 16500
    encode_value(frame, 0, 16, 16500);
    
    // Serial Number: 9999
    encode_value(frame, 16, 14, 9999);
    
    // Country Code: 250 (USA)
    encode_value(frame, 30, 10, 250);
    
    // Status bits
    frame[40] = 1;  // Homing active
    frame[41] = 0;  // No RLS
    frame[42] = 0;  // Normal operation
    
    // Position: New York (40.7128°N, 74.0060°W)
    encode_position(frame, 40.7128, -74.0060);
    
    // Vessel ID Type: 010 (Radio Call Sign)
    encode_value(frame, 90, 3, 2);
    
    // Call Sign: "N123AB"
    encode_callsign(frame, 93, "N123AB");
    
    // Beacon Type: 011 (ELT(DT))
    encode_value(frame, 137, 3, 3);
    
    // Spare bits: all 1's
    for (int i = 140; i < 154; i++) frame[i] = 1;
    
    // Rotating Field #1 (In-Flight Emergency)
    encode_value(frame, 154, 4, 1);  // Field ID
    encode_value(frame, 158, 17, 43200); // 12:00:00 UTC
    encode_value(frame, 175, 10, (10000 + 400) / 16); // 10km altitude
    encode_value(frame, 185, 4, 4);  // G-switch activation
    encode_value(frame, 189, 2, 2);  // 3D fix
    encode_value(frame, 191, 2, 1);  // 33-66% battery
    
    // BCH dummy
    for (int i = 202; i < 250; i++) {
        frame[i] = ((i * 3) % 11 < 5) ? 1 : 0;
    }
    
    bits_to_hex(frame, 250, hex_output);
    printf("Hex: %s\n", hex_output);
    printf("Longueur: %lu caractères (%d bits)\n\n", strlen(hex_output), 250);
    
    // === EXEMPLE 3: PLB avec RLS TWC ===
    printf("3. PLB avec RLS Type 3 TWC:\n");
    memset(frame, 0, 250);
    
    // TAC Number: 14000
    encode_value(frame, 0, 16, 14000);
    
    // Serial Number: 777
    encode_value(frame, 16, 14, 777);
    
    // Country Code: 232 (UK)
    encode_value(frame, 30, 10, 232);
    
    // Status bits
    frame[40] = 1;  // Homing active
    frame[41] = 1;  // RLS enabled
    frame[42] = 0;  // Normal operation
    
    // Position par défaut (no position capability)
    frame[43] = 0;  // N/S
    encode_value(frame, 44, 7, 0x7F);    // 1111111
    encode_value(frame, 51, 15, 0x07C0); // 000001111100000
    frame[66] = 1;  // E/W
    encode_value(frame, 67, 8, 0xFF);    // 11111111
    encode_value(frame, 75, 15, 0x783F); // 111110000011111
    
    // Vessel ID Type: 000 (None)
    encode_value(frame, 90, 3, 0);
    
    // Beacon Type: 010 (PLB)
    encode_value(frame, 137, 3, 2);
    
    // Spare bits: all 1's
    for (int i = 140; i < 154; i++) frame[i] = 1;
    
    // Rotating Field #4 (TWC)
    encode_value(frame, 154, 4, 4);  // Field ID
    encode_value(frame, 158, 3, 1);  // Galileo provider
    encode_value(frame, 161, 5, 3);  // Version 3
    frame[166] = 1; // TWC ACK received
    encode_value(frame, 169, 7, 15); // Question A
    encode_value(frame, 176, 4, 2);  // Answer A
    encode_value(frame, 180, 7, 22); // Question B  
    encode_value(frame, 187, 4, 1);  // Answer B
    encode_value(frame, 191, 7, 8);  // Question C
    encode_value(frame, 198, 4, 0);  // Answer C
    
    // BCH dummy
    for (int i = 202; i < 250; i++) {
        frame[i] = (i % 5 < 2) ? 1 : 0;
    }
    
    bits_to_hex(frame, 250, hex_output);
    printf("Hex: %s\n", hex_output);
    printf("Longueur: %lu caractères (%d bits)\n\n", strlen(hex_output), 250);
    
    printf("=== INSTRUCTIONS D'UTILISATION ===\n");
    printf("Pour tester ces trames avec le décodeur :\n");
    printf("1. Modifier dec406_main.c pour accepter les trames 250 bits (64 caractères hex)\n");
    printf("2. Utiliser : ./dec406_hex <trame_hexadecimale>\n");
    printf("3. Le décodeur 2G analysera automatiquement la trame\n\n");
    
    printf("Note: Les codes BCH sont simplifiés pour les tests.\n");
    printf("En production, utiliser un générateur BCH(250,202) complet.\n");
    
    return 0;
}