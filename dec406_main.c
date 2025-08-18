// dec406_main.c - Version corrigée
#include "dec406.h"
#include <stdio.h>
#include <stdlib.h>

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
    printf("=== Tests du décodeur 406 MHz avec données réalistes ===\n");
    
    // Test 1: EPIRB française avec MMSI et position
    uint8_t epirb_france[FRAME_1G_LENGTH] = {
        // Sync pattern (24 bits) - Pattern standard COSPAS-SARSAT
        1,0,1,1,0,0,1,0,1,1,0,0,1,0,1,0,0,1,1,0,0,1,0,1,
        // Frame format (0 = short frame)
        0,
        // Fixed bits
        1,
        // Country code: France = 226 (0x0E2 = 001110010)
        0,0,1,1,1,0,0,1,0,
        // Protocol: Standard Location (010) + Customer Flag
        0,1,0,1,
        // Serial number (18 bits) - Exemple: 123456
        0,0,0,1,1,1,1,0,0,0,1,0,0,1,0,0,0,0,
        // Beacon type + autres données
        // Position: Lat 43.5°N (Monaco), Lon 7.4°E
        // NS=0 (North), Lat=43°, Min=30'
        0,0,1,0,1,0,1,1,1,1,0,
        // EW=0 (East), Lon=7°, Min=24'  
        0,0,0,0,0,1,1,1,0,0,1,
        // ID Type: MMSI (001)
        0,0,1,
        // MMSI: 227001234 (exemple français) - 30 bits
        0,0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,1,1,0,0,1,0,0,1,0,1,0,0,1,0
    };
    
    // Remplir le reste avec des zéros (CRC et padding)
    for (int i = 85; i < FRAME_1G_LENGTH; i++) {
        epirb_france[i] = 0;
    }
    
    // Test 2: PLB Personnel avec call sign
    uint8_t plb_callsign[FRAME_1G_LENGTH] = {
        // Sync pattern
        1,0,1,1,0,0,1,0,1,1,0,0,1,0,1,0,0,1,1,0,0,1,0,1,
        // Short frame
        0,1,
        // Country: USA = 366 (0x16E = 101101110)
        0,1,0,1,1,0,1,1,1,0,
        // Protocol: User protocol (001)
        0,0,1,1,
        // Serial
        0,0,1,0,0,1,1,0,1,0,0,1,1,0,1,0,0,0,
        // Position non disponible (tous à 1)
        1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        // ID Type: Radio Call Sign (010)  
        0,1,0,
        // Call sign "N123AB" en Baudot (7 caractères × 6 bits = 42 bits)
        1,0,0,1,1,0, // N
        0,0,1,1,0,1, // 1
        0,1,1,0,0,1, // 2
        0,1,0,0,0,0, // 3
        1,1,1,0,0,0, // A
        1,1,0,0,1,1, // B
        1,0,0,1,0,0  // Space/padding
    };
    
    // Remplir le reste avec des zéros
    for (int i = 85; i < FRAME_1G_LENGTH; i++) {
        plb_callsign[i] = 0;
    }
    
    // Test 3: Trame 2G ELT avec position et données d'urgence
    uint8_t elt_emergency[FRAME_2G_LENGTH] = {0};
    
    // Construction manuelle d'une trame 2G réaliste
    // TAC: 12345 (16 bits) = 0x3039
    elt_emergency[0] = 0; elt_emergency[1] = 0; elt_emergency[2] = 1; elt_emergency[3] = 1;
    elt_emergency[4] = 0; elt_emergency[5] = 0; elt_emergency[6] = 0; elt_emergency[7] = 0;
    elt_emergency[8] = 0; elt_emergency[9] = 0; elt_emergency[10] = 1; elt_emergency[11] = 1;
    elt_emergency[12] = 1; elt_emergency[13] = 0; elt_emergency[14] = 0; elt_emergency[15] = 1;
    
    // Serial: 6789 (14 bits)
    elt_emergency[16] = 0; elt_emergency[17] = 0; elt_emergency[18] = 0; elt_emergency[19] = 1;
    elt_emergency[20] = 1; elt_emergency[21] = 0; elt_emergency[22] = 1; elt_emergency[23] = 0;
    elt_emergency[24] = 0; elt_emergency[25] = 1; elt_emergency[26] = 1; elt_emergency[27] = 0;
    elt_emergency[28] = 0; elt_emergency[29] = 1;
    
    // Country: 250 (10 bits) = 0x0FA
    elt_emergency[30] = 0; elt_emergency[31] = 0; elt_emergency[32] = 1; elt_emergency[33] = 1;
    elt_emergency[34] = 1; elt_emergency[35] = 1; elt_emergency[36] = 1; elt_emergency[37] = 0;
    elt_emergency[38] = 1; elt_emergency[39] = 0;
    
    // Flags: Homing=1, RLS=0, Test=0
    elt_emergency[40] = 1; elt_emergency[41] = 0; elt_emergency[42] = 0;
    
    // Position: 45.5°N, 2.3°E (simulé)
    elt_emergency[43] = 1; // Position available flag
    elt_emergency[44] = 0; // NS flag (0=North)
    
    // Latitude: 45° = 45 en 7 bits (0101101)
    elt_emergency[45] = 0; elt_emergency[46] = 1; elt_emergency[47] = 0;
    elt_emergency[48] = 1; elt_emergency[49] = 1; elt_emergency[50] = 0;
    elt_emergency[51] = 1;
    
    // Fraction latitude (0.5° = 16384 en 15 bits)
    elt_emergency[52] = 0; elt_emergency[53] = 1; elt_emergency[54] = 0;
    elt_emergency[55] = 0; elt_emergency[56] = 0; elt_emergency[57] = 0;
    elt_emergency[58] = 0; elt_emergency[59] = 0; elt_emergency[60] = 0;
    elt_emergency[61] = 0; elt_emergency[62] = 0; elt_emergency[63] = 0;
    elt_emergency[64] = 0; elt_emergency[65] = 0; elt_emergency[66] = 0;
    
    // EW flag (0=East)
    elt_emergency[67] = 0;
    
    // Longitude: 2° = 2 en 8 bits (00000010)
    elt_emergency[68] = 0; elt_emergency[69] = 0; elt_emergency[70] = 0;
    elt_emergency[71] = 0; elt_emergency[72] = 0; elt_emergency[73] = 0;
    elt_emergency[74] = 1; elt_emergency[75] = 0;
    
    // Fraction longitude (0.3° ≈ 9830 en 15 bits)
    elt_emergency[76] = 0; elt_emergency[77] = 0; elt_emergency[78] = 1;
    elt_emergency[79] = 0; elt_emergency[80] = 0; elt_emergency[81] = 1;
    elt_emergency[82] = 1; elt_emergency[83] = 0; elt_emergency[84] = 0;
    elt_emergency[85] = 1; elt_emergency[86] = 1; elt_emergency[87] = 0;
    elt_emergency[88] = 0; elt_emergency[89] = 1; elt_emergency[90] = 1;
    
    // ID Type: MMSI (001)
    elt_emergency[90] = 0; elt_emergency[91] = 0; elt_emergency[92] = 1;
    
    // MMSI: 250001234 (exemple)
    uint32_t mmsi = 250001234;
    for (int i = 0; i < 30; i++) {
        elt_emergency[93 + i] = (mmsi >> (29 - i)) & 1;
    }
    
    // Beacon type: ELT(DT) = 3 (011)
    elt_emergency[137] = 0; elt_emergency[138] = 1; elt_emergency[139] = 1;
    
    // Rotating field: In-Flight Emergency (ID=1)
    elt_emergency[154] = 0; elt_emergency[155] = 0; elt_emergency[156] = 0; elt_emergency[157] = 1;
    
    // Time last location (17 bits) - exemple: 3600 secondes
    uint32_t time_last = 3600;
    for (int i = 0; i < 17; i++) {
        elt_emergency[158 + i] = (time_last >> (16 - i)) & 1;
    }
    
    // Altitude (10 bits) - exemple: 10000m -> (10000+400)/16 = 650
    uint16_t alt_code = 650;
    for (int i = 0; i < 10; i++) {
        elt_emergency[175 + i] = (alt_code >> (9 - i)) & 1;
    }
    
    // Triggering event: G-switch activation (0100)
    elt_emergency[185] = 0; elt_emergency[186] = 1; elt_emergency[187] = 0; elt_emergency[188] = 0;
    
    // GNSS status: 3D fix (10)
    elt_emergency[189] = 1; elt_emergency[190] = 0;
    
    // Battery capacity: 75% (11)
    elt_emergency[191] = 1; elt_emergency[192] = 1;
    
    printf("\n=== Test 1: EPIRB française avec MMSI ===");
    decode_beacon(epirb_france, FRAME_1G_LENGTH);
    
    printf("\n=== Test 2: PLB américain avec indicatif ===");
    decode_beacon(plb_callsign, FRAME_1G_LENGTH);
    
    printf("\n=== Test 3: ELT d'urgence avec position ===");
    decode_beacon(elt_emergency, FRAME_2G_LENGTH);
    
    printf("\n=== Tests terminés ===\n");
    printf("\nℹ️  Les erreurs BCH sont normales avec des données de test.\n");
    printf("💡 Pour des tests avec de vraies trames, utilisez des données\n");
    printf("   capturées depuis un récepteur 406 MHz réel.\n");
    
    return EXIT_SUCCESS;
}
