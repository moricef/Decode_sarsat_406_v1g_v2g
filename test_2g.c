#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "dec406.h"

int main() {
    printf("=== TEST DU DECODEUR 2G AMELIORE ===\n");
    
    // Trame 2G simulée (250 bits) avec des valeurs de test
    uint8_t test_frame[250] = {0};
    
    // Configuration des bits de test selon les spécifications T.018
    // TAC Number (bits 1-16): 12345 (0x3039)
    test_frame[0] = 0; test_frame[1] = 0; test_frame[2] = 1; test_frame[3] = 1;
    test_frame[4] = 0; test_frame[5] = 0; test_frame[6] = 0; test_frame[7] = 0;
    test_frame[8] = 0; test_frame[9] = 0; test_frame[10] = 1; test_frame[11] = 1;
    test_frame[12] = 1; test_frame[13] = 0; test_frame[14] = 0; test_frame[15] = 1;
    
    // Serial Number (bits 17-30): 1234 (0x04D2)
    test_frame[16] = 0; test_frame[17] = 0; test_frame[18] = 0; test_frame[19] = 0;
    test_frame[20] = 0; test_frame[21] = 1; test_frame[22] = 0; test_frame[23] = 0;
    test_frame[24] = 1; test_frame[25] = 1; test_frame[26] = 0; test_frame[27] = 1;
    test_frame[28] = 0; test_frame[29] = 0;
    
    // Country Code (bits 31-40): 257 (France) (0x101)
    test_frame[30] = 0; test_frame[31] = 1; test_frame[32] = 0; test_frame[33] = 0;
    test_frame[34] = 0; test_frame[35] = 0; test_frame[36] = 0; test_frame[37] = 0;
    test_frame[38] = 0; test_frame[39] = 1;
    
    // Status bits
    test_frame[40] = 1;  // Homing device active
    test_frame[41] = 1;  // RLS enabled
    test_frame[42] = 0;  // Normal operation (not test)
    
    // Position par défaut (no position capability)
    // Latitude: 0 1111111 000001111100000
    test_frame[43] = 0;  // N/S flag
    // Degrees (bits 44-50): 1111111
    for (int i = 44; i < 51; i++) test_frame[i] = 1;
    // Fraction (bits 51-65): 000001111100000
    test_frame[51] = 0; test_frame[52] = 0; test_frame[53] = 0; test_frame[54] = 0;
    test_frame[55] = 0; test_frame[56] = 1; test_frame[57] = 1; test_frame[58] = 1;
    test_frame[59] = 1; test_frame[60] = 1; test_frame[61] = 1; test_frame[62] = 0;
    test_frame[63] = 0; test_frame[64] = 0; test_frame[65] = 0;
    
    // Longitude: 1 11111111 111110000011111
    test_frame[66] = 1;  // E/W flag
    // Degrees (bits 67-74): 11111111
    for (int i = 67; i < 75; i++) test_frame[i] = 1;
    // Fraction (bits 75-89): 111110000011111
    test_frame[75] = 1; test_frame[76] = 1; test_frame[77] = 1; test_frame[78] = 1;
    test_frame[79] = 1; test_frame[80] = 0; test_frame[81] = 0; test_frame[82] = 0;
    test_frame[83] = 0; test_frame[84] = 0; test_frame[85] = 1; test_frame[86] = 1;
    test_frame[87] = 1; test_frame[88] = 1; test_frame[89] = 1;
    
    // Vessel ID Type (bits 91-93): 001 (MMSI)
    test_frame[90] = 0; test_frame[91] = 0; test_frame[92] = 1;
    
    // MMSI (bits 94-123): 123456789 pour test
    uint32_t test_mmsi = 123456789;
    for (int i = 0; i < 30; i++) {
        test_frame[93 + i] = (test_mmsi >> (29 - i)) & 1;
    }
    
    // AIS suffix default (bits 124-137): 10101010101010
    for (int i = 123; i < 137; i++) {
        test_frame[i] = (i - 123) % 2;
    }
    
    // Beacon Type (bits 138-140): 001 (EPIRB)
    test_frame[137] = 0; test_frame[138] = 0; test_frame[139] = 1;
    
    // Spare bits (bits 141-154): all 1's for normal
    for (int i = 140; i < 154; i++) test_frame[i] = 1;
    
    // Rotating Field #0 (G.008 Objective Requirements)
    // Field ID (bits 155-158): 0000
    test_frame[154] = 0; test_frame[155] = 0; test_frame[156] = 0; test_frame[157] = 0;
    
    // Elapsed time (bits 159-164): 5 hours
    test_frame[158] = 0; test_frame[159] = 0; test_frame[160] = 0;
    test_frame[161] = 1; test_frame[162] = 0; test_frame[163] = 1;
    
    // Time from last location (bits 165-175): 30 minutes
    uint16_t time_loc = 30;
    for (int i = 0; i < 11; i++) {
        test_frame[164 + i] = (time_loc >> (10 - i)) & 1;
    }
    
    // Altitude (bits 176-185): 1000m
    uint16_t alt_code = (1000 + 400) / 16;  // Encoding: (alt + 400) / 16
    for (int i = 0; i < 10; i++) {
        test_frame[175 + i] = (alt_code >> (9 - i)) & 1;
    }
    
    // DOP values (bits 186-193): HDOP=2, VDOP=3
    test_frame[185] = 0; test_frame[186] = 0; test_frame[187] = 1; test_frame[188] = 0; // HDOP=2
    test_frame[189] = 0; test_frame[190] = 0; test_frame[191] = 1; test_frame[192] = 1; // VDOP=3
    
    // Activation (bits 194-195): 00 (Manual)
    test_frame[193] = 0; test_frame[194] = 0;
    
    // Battery (bits 196-198): 101 (75-100%)
    test_frame[195] = 1; test_frame[196] = 0; test_frame[197] = 1;
    
    // GNSS Status (bits 199-200): 10 (3D fix)
    test_frame[198] = 1; test_frame[199] = 0;
    
    // Spare rotating field bits (bits 201-202): 00
    test_frame[200] = 0; test_frame[201] = 0;
    
    // BCH error correction bits (bits 203-250): simplified for test
    for (int i = 202; i < 250; i++) test_frame[i] = 0;
    
    printf("Test avec trame 2G simulée (EPIRB française avec MMSI):\n");
    decode_2g(test_frame);
    
    return 0;
}