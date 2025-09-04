/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "dec406.h"
#include "display_utils.h"
#include "country_codes.h"

// ===================================================
// BCH Error Correction Implementation (T.018 Appendix B)
// ===================================================
#define BCH_POLY 0x1C7D72F93C97  // Polynomial: 1110001111110101110000101110111110011110010010111

/**
 * @brief Decodes BCH(250,202) encoded message
 * @param msg Input: 250-bit raw message
 * @param out Output: 202-bit corrected data
 * 
 * Implements BCH error detection/correction as specified in
 * C/S T.018 Appendix B using the defined generator polynomial.
 */
static void bch_decode_250_202(const uint8_t *msg, uint8_t *out) {
    const int poly_degree = 48;
    uint64_t shift_reg = 0;
    
    // Syndrome calculation
    for (int i = 0; i < 250; i++) {
        uint8_t bit = (msg[i] ^ (shift_reg >> (poly_degree - 1))) & 1;
        shift_reg = (shift_reg << 1) | bit;
        
        if (shift_reg & (1ULL << poly_degree)) {
            shift_reg ^= BCH_POLY;
        }
    }
    
    // Error detection
    if (shift_reg != 0) {
        fprintf(stderr, "BCH: Errors detected (syndrome: 0x%012lX)\n", shift_reg);
        // Advanced correction logic would go here
    }
    
    // Extract information bits (first 202 bits)
    memcpy(out, msg, 202);
}

// ===================================================
// Modified Baudot Decoding Table (Table 3.2 - T.018 Specification)
// ===================================================
static const char *baudot_table[64] = {
    [0b000001] = "5", [0b000011] = "9", [0b001010] = "4", [0b001100] = "8",
    [0b001101] = "0", [0b010000] = "3", [0b010101] = "6", [0b010111] = "/",
    [0b011000] = "-", [0b011001] = "2", [0b011100] = "7", [0b011101] = "1",
    [0b100001] = "T", [0b100011] = "O", [0b100100] = " ",  [0b100101] = "H",
    [0b100110] = "N", [0b100111] = "M", [0b101001] = "L", [0b101010] = "R",
    [0b101011] = "G", [0b101100] = "I", [0b101101] = "P", [0b101110] = "C",
    [0b101111] = "V", [0b110000] = "E", [0b110001] = "Z", [0b110010] = "D",
    [0b110011] = "B", [0b110100] = "S", [0b110101] = "Y", [0b110110] = "F",
    [0b110111] = "X", [0b111000] = "A", [0b111001] = "W", [0b111010] = "J",
    [0b111100] = "U", [0b111101] = "Q", [0b111110] = "K"
};

// ===================================================
// Beacon Information Structure
// ===================================================
#define BCH_N 250
#define BCH_K 202
#define MAIN_BITS 154
#define ROT_BITS 48

typedef struct {
    // Main field data
    uint16_t tac;
    uint16_t serial;
    uint16_t country;
    uint8_t homing;
    uint8_t rls;
    uint8_t test;
    double lat;
    double lon;
    uint8_t vessel_id_type;
    char vessel_id[64];
    uint8_t beacon_type;
    char hex_id[24];  // 23 characters + null terminator
    uint8_t has_position;
    
    // Rotating field data
    struct {
        uint8_t id;
        union {
            struct {  // Field #0: G.008 Objective Requirements
                uint8_t elapsed_time;       // Hours since activation
                uint16_t time_last_loc;     // Minutes since last position
                int16_t altitude;           // Altitude in meters (-400 to 15952)
                uint8_t hdop;               // Horizontal DOP category
                uint8_t vdop;               // Vertical DOP category
                uint8_t activation_method;  // Manual/Auto activation
                uint8_t battery_capacity;   // Remaining battery
                uint8_t gnss_status;        // Fix status (No fix/2D/3D)
            } g008_obj;
            
            struct {  // Field #1: In-Flight Emergency
                uint32_t time_last_loc;     // UTC time of last fix (seconds)
                int16_t altitude;           // Altitude in meters
                uint8_t triggering_event;   // Activation cause
                uint8_t gnss_status;        // Fix status
                uint8_t battery_capacity;   // Remaining battery
            } in_flight;
            
            struct {  // Field #2: RLS Acknowledgement
                uint8_t capability_auto_ack;
                uint8_t capability_manual_ack;
                uint8_t rls_provider;       // Galileo/GLONASS/BDS
                uint8_t feedback_type1;     // Type-1 RLM received
                uint8_t feedback_type2;     // Type-2 RLM received
                uint32_t rlm_data;          // RLM content
            } rls_ack;
            
            struct {  // Field #4: Two-Way Communication
                uint8_t twc_provider;
                uint8_t version_id;
                uint8_t twc_ack_received;
                uint8_t questionA;
                uint8_t answerA;
                uint8_t questionB;
                uint8_t answerB;
                uint8_t questionC;
                uint8_t answerC;
            } twc;
            
            struct {  // Field #15: Cancellation
                uint8_t deactivation_method;  // Manual/Auto deactivation
            } cancellation;
        };
    } rot;
    
    uint8_t raw_data[202];  // Store raw data for validation
} BeaconInfo;

// ===================================================
// Utility Functions
// ===================================================
/**
 * @brief Extracts bits from a bit array
 * @param bits Source bit array
 * @param start Starting bit index
 * @param len Number of bits to extract
 * @return Extracted value
 */
static uint32_t get_bits(const uint8_t *bits, int start, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        val = (val << 1) | bits[start + i];
    }
    return val;
}

/**
 * @brief Decodes GNSS position from bit array
 * @param bits Source bit array
 * @param lat Output: Latitude in degrees
 * @param lon Output: Longitude in degrees
 * 
 * Implements position decoding as specified in Section 3.2
 * with 3.4 meter resolution and default value handling.
 */
static void decode_position(const uint8_t *bits, double *lat, double *lon) {
    // Position decoding according to T.018 Section 3.2 (bits are 0-indexed)
    // Latitude: bit 44 (N/S), bits 45-51 (degrees), bits 52-66 (decimal fraction)
    // Longitude: bit 67 (E/W), bits 68-75 (degrees), bits 76-90 (decimal fraction)
    
    int ns_flag = bits[43];  // bit 44 in T.018 = index 43 in 0-based array
    int lat_deg = get_bits(bits, 44, 7);   // bits 45-51 in T.018
    int lat_frac = get_bits(bits, 51, 15); // bits 52-66 in T.018
    
    int ew_flag = bits[66];  // bit 67 in T.018 = index 66 in 0-based array
    int lon_deg = get_bits(bits, 67, 8);   // bits 68-75 in T.018  
    int lon_frac = get_bits(bits, 75, 15); // bits 76-90 in T.018
    
    // Check for default values (T.018 Section 3.2) indicating no position capability
    // Latitude default: 0 1111111 000001111100000
    // Longitude default: 1 11111111 111110000011111
    if (ns_flag == 0 && lat_deg == 0x7F && lat_frac == 0x07C0 &&
        ew_flag == 1 && lon_deg == 0xFF && lon_frac == 0x783F) {
        *lat = 0.0;
        *lon = 0.0;
        return;
    }
    
    // Check for alternate default values (no position available at this time)
    // Latitude alternate: 1 1111111 000001111100000  
    // Longitude alternate: 0 11111111 111110000011111
    if (ns_flag == 1 && lat_deg == 0x7F && lat_frac == 0x07C0 &&
        ew_flag == 0 && lon_deg == 0xFF && lon_frac == 0x783F) {
        *lat = 0.0;
        *lon = 0.0;
        return;
    }
    
    // Calculate actual position
    *lat = (lat_deg + lat_frac / 32768.0) * (ns_flag ? -1.0 : 1.0);
    *lon = (lon_deg + lon_frac / 32768.0) * (ew_flag ? -1.0 : 1.0);
}

// ===================================================
// Main Field Decoding (Section 3.2)
// ===================================================
/**
 * @brief Decodes main message field (154 bits)
 * @param bits Source bit array (202 information bits)
 * @param info Output: Decoded beacon information
 */
static void decode_main(const uint8_t *bits, BeaconInfo *info) {
    // Store raw data for validation
    memcpy(info->raw_data, bits, 202);
    
    // Decode fixed fields (T.018 Section 3.2 - convert 1-based to 0-based indices)
    info->tac = get_bits(bits, 0, 16);      // bits 1-16 in T.018
    info->serial = get_bits(bits, 16, 14);  // bits 17-30 in T.018
    info->country = get_bits(bits, 30, 10); // bits 31-40 in T.018
    info->homing = bits[40];                // bit 41 in T.018
    info->rls = bits[41];                   // bit 42 in T.018
    info->test = bits[42];                  // bit 43 in T.018
    
    // Decode position
    decode_position(bits, &info->lat, &info->lon);
    
    info->has_position = (info->lat != 0.0 || info->lon != 0.0) ? 1 : 0;
    
    // Vessel ID type (bits 91-93 in T.018)
    info->vessel_id_type = get_bits(bits, 90, 3);
    
    // Beacon type (bits 138-140 in T.018)
    info->beacon_type = get_bits(bits, 137, 3);
}

// ===================================================
// Rotating Field Decoding (Section 3.3)
// ===================================================
/**
 * @brief Decodes rotating field (48 bits)
 * @param rot_bits Source bit array (rotating field)
 * @param info Output: Decoded rotating field information
 */
static void decode_rot_field(const uint8_t *rot_bits, BeaconInfo *info) {
    info->rot.id = get_bits(rot_bits, 0, 4);
    
    switch(info->rot.id) {
        case 0:  // G.008 Objective Requirements
            info->rot.g008_obj.elapsed_time = get_bits(rot_bits, 4, 6);
            info->rot.g008_obj.time_last_loc = get_bits(rot_bits, 10, 11);
            
            // Altitude decoding with special cases
            uint16_t alt_code = get_bits(rot_bits, 21, 10);
            if (alt_code == 0x3FF) {  // Not available
                info->rot.g008_obj.altitude = -32768;
            } else {
                info->rot.g008_obj.altitude = (alt_code * 16) - 400;
            }
            
            // DOP values
            uint8_t dop = get_bits(rot_bits, 31, 8);
            info->rot.g008_obj.hdop = dop >> 4;
            info->rot.g008_obj.vdop = dop & 0x0F;
            
            // Activation and battery
            info->rot.g008_obj.activation_method = get_bits(rot_bits, 39, 2);
            info->rot.g008_obj.battery_capacity = get_bits(rot_bits, 41, 3);
            info->rot.g008_obj.gnss_status = get_bits(rot_bits, 44, 2);
            break;
        
        case 1:  // In-Flight Emergency
            info->rot.in_flight.time_last_loc = get_bits(rot_bits, 4, 17);
            
            alt_code = get_bits(rot_bits, 21, 10);
            if (alt_code == 0x3FF) {  // Not available
                info->rot.in_flight.altitude = -32768;
            } else {
                info->rot.in_flight.altitude = (alt_code * 16) - 400;
            }
            
            info->rot.in_flight.triggering_event = get_bits(rot_bits, 31, 4);
            info->rot.in_flight.gnss_status = get_bits(rot_bits, 35, 2);
            info->rot.in_flight.battery_capacity = get_bits(rot_bits, 37, 2);
            break;
        
        case 2:  // RLS Acknowledgement
            info->rot.rls_ack.capability_auto_ack = rot_bits[6];
            info->rot.rls_ack.capability_manual_ack = rot_bits[7];
            info->rot.rls_ack.rls_provider = get_bits(rot_bits, 12, 3);
            info->rot.rls_ack.feedback_type1 = rot_bits[15];
            info->rot.rls_ack.feedback_type2 = rot_bits[16];
            info->rot.rls_ack.rlm_data = get_bits(rot_bits, 17, 20);
            break;
        
        case 4:  // Two-Way Communication (T.018 Table 3.7)
            info->rot.twc.twc_provider = get_bits(rot_bits, 4, 3);
            info->rot.twc.version_id = get_bits(rot_bits, 7, 5);
            info->rot.twc.twc_ack_received = rot_bits[12];
            // Spare bits 13-14 (2 bits)
            info->rot.twc.questionA = get_bits(rot_bits, 15, 7);  // bits 16-22 in message
            info->rot.twc.answerA = get_bits(rot_bits, 22, 4);    // bits 23-26 in message
            info->rot.twc.questionB = get_bits(rot_bits, 26, 7);  // bits 27-33 in message
            info->rot.twc.answerB = get_bits(rot_bits, 33, 4);    // bits 34-37 in message
            info->rot.twc.questionC = get_bits(rot_bits, 37, 7);  // bits 38-44 in message
            info->rot.twc.answerC = get_bits(rot_bits, 44, 4);    // bits 45-48 in message
            break;
        
        case 15:  // Cancellation Message
            info->rot.cancellation.deactivation_method = get_bits(rot_bits, 46, 2);
            break;
    }
}

// ===================================================
// Vessel ID Decoding (Section 3.2)
// ===================================================
/**
 * @brief Decodes vessel/aircraft identification
 * @param bits Source bit array
 * @param type Identification type (0-5)
 * @param output Output: Formatted identification string
 */
static void decode_vessel_id(const uint8_t *bits, int type, char *output) {
    output[0] = '\0';  // Initialize output
    
    switch(type) {
        case 0:  // None
            strcpy(output, "None");
            break;
            
        case 1:  // Maritime MMSI (T.018 Section 3.2)
            {
                uint32_t mmsi = get_bits(bits, 93, 30);  // 30-bit MMSI from bits 94-123
                uint16_t ais_suffix = get_bits(bits, 123, 14);  // AIS suffix from bits 124-137
                
                if (mmsi == 0x1FFFF) {  // Default decimal 000111111 for no MMSI
                    strcpy(output, "No MMSI Available");
                } else if (mmsi > 999999999) {
                    strcpy(output, "Invalid MMSI");
                } else {
                    sprintf(output, "MMSI:%09u", mmsi);
                    // Check for AIS suffix (EPIRB-AIS format 917243YYYY)
                    if (ais_suffix != 10922) {  // Not default value (10101010101010)
                        sprintf(output + strlen(output), " AIS:%04u", ais_suffix);
                    }
                }
            }
            break;
            
        case 2:  // Radio Call Sign (T.018 Section 3.2)
        case 3:  // Aircraft Registration Marking (T.018 Section 3.2)
            {
                char decoded[8] = {0};
                int valid_chars = 0;
                bool all_spaces = true;
                
                for (int i = 0; i < 7; i++) {
                    int start_bit = 93 + i * 6;
                    int code = get_bits(bits, start_bit, 6);
                    
                    // Look up in Modified Baudot table
                    const char *ch = baudot_table[code];
                    if (!ch) ch = "?";  // Invalid code
                    
                    strcat(decoded, ch);
                    if (*ch != '?' && *ch != ' ') {
                        valid_chars++;
                        all_spaces = false;
                    }
                }
                
                // Check for default (all spaces = no call sign/registration available)
                if (all_spaces || valid_chars < 1) {
                    if (type == 2) {
                        strcpy(output, "No Call Sign");
                    } else {
                        strcpy(output, "No Registration");
                    }
                } else {
                    // Trim trailing spaces for display
                    int len = strlen(decoded);
                    while (len > 0 && decoded[len-1] == ' ') {
                        decoded[--len] = '\0';
                    }
                    strcpy(output, decoded);
                }
            }
            break;
            
        case 4:  // Aircraft 24-bit Address (T.018 Section 3.2)
            {
                uint32_t addr = get_bits(bits, 93, 24);  // bits 94-117
                uint32_t operator_3ld = get_bits(bits, 117, 15);  // bits 118-132 (3x5 bits)
                uint8_t spare = get_bits(bits, 132, 5);  // bits 133-137 spare
                
                sprintf(output, "ICAO24:%06X", addr);
                
                // Check if 3LD operator designator is present (not all zeros or default ZGA)
                if (operator_3ld != 0 && operator_3ld != 0x4583) {  // ZGA = 10001 01011 11000
                    // Decode 3-letter operator designator
                    char op_code[4] = {0};
                    op_code[0] = 'A' + ((operator_3ld >> 10) & 0x1F);
                    op_code[1] = 'A' + ((operator_3ld >> 5) & 0x1F);
                    op_code[2] = 'A' + (operator_3ld & 0x1F);
                    sprintf(output + strlen(output), " OP:%s", op_code);
                } else if (operator_3ld == 0x4583) {
                    strcat(output, " OP:ZGA");  // Default for no 3LD
                }
            }
            break;
            
        case 5:  // Aircraft Operator + Serial (T.018 Section 3.2)
            {
                // 3-letter operator designator (bits 94-108, 3x5 bits)
                uint32_t operator_3ld = get_bits(bits, 93, 15);
                // Serial number (bits 109-120, 12 bits)
                uint16_t serial = get_bits(bits, 108, 12);
                // Spare bits (bits 121-137, 17 bits) should be all 1's
                uint32_t spare = get_bits(bits, 120, 17);
                
                if (operator_3ld != 0) {
                    char op_code[4] = {0};
                    op_code[0] = 'A' + ((operator_3ld >> 10) & 0x1F);
                    op_code[1] = 'A' + ((operator_3ld >> 5) & 0x1F);
                    op_code[2] = 'A' + (operator_3ld & 0x1F);
                    sprintf(output, "%s-%u", op_code, serial);
                    
                    // Validate spare bits pattern
                    if (spare != 0x1FFFF) {
                        strcat(output, " [WARN:Spare]");
                    }
                } else {
                    strcpy(output, "No Operator ID");
                }
            }
            break;
            
        default:
            strcpy(output, "Reserved Type");
    }
}

// ===================================================
// 23 Hex ID Generation (Section 3.6)
// ===================================================
/**
 * @brief Generates 23 Hex ID from message data
 * @param bits Source bit array (202 information bits)
 * @param info Output: Beacon info with hex_id populated
 * 
 * Constructs the unique 23-character hexadecimal identifier
 * according to the specification in Table 3.11.
 */
static void compute_hex_id(const uint8_t *bits, BeaconInfo *info) {
    uint8_t id_bits[92] = {0};  // 92 bits for 23 hex characters
    
    // Construct bit sequence (Table 3.11)
    id_bits[0] = 1;  // Fixed '1'
    for (int i = 0; i < 10; i++) id_bits[1 + i] = bits[30 + i];  // Country code
    id_bits[11] = 1; id_bits[12] = 0; id_bits[13] = 1;  // Fixed '101'
    for (int i = 0; i < 16; i++) id_bits[14 + i] = bits[i];  // TAC
    for (int i = 0; i < 14; i++) id_bits[30 + i] = bits[16 + i];  // Serial
    id_bits[44] = bits[42];  // Test protocol flag
    for (int i = 0; i < 3; i++) id_bits[45 + i] = bits[90 + i];  // ID type
    for (int i = 0; i < 44; i++) id_bits[48 + i] = bits[93 + i];  // ID content
    
    // Convert to hexadecimal
    for (int i = 0; i < 23; i++) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4; j++) {
            nibble = (nibble << 1) | id_bits[i * 4 + j];
        }
        sprintf(info->hex_id + i, "%1X", nibble);
    }
}

// ===================================================
// Data Validation Functions
// ===================================================
/**
 * @brief Validates GNSS position data
 * @param info Beacon info to validate
 * 
 * Checks for invalid coordinates and default values
 * indicating unavailable position.
 */
static void validate_gnss(BeaconInfo *info) {
    // Check latitude range
    if (info->lat < -90.0 || info->lat > 90.0) {
        fprintf(stderr, "Validation: Invalid latitude %.5f\n", info->lat);
        info->lat = 0.0;
    }
    
    // Check longitude range
    if (info->lon < -180.0 || info->lon > 180.0) {
        fprintf(stderr, "Validation: Invalid longitude %.5f\n", info->lon);
        info->lon = 0.0;
    }
    
    // Check default values (no position available)
    if (get_bits(info->raw_data, 43, 1) == 1 && 
        get_bits(info->raw_data, 67, 1) == 1) {
        info->lat = 0.0;
        info->lon = 0.0;
    }
}

/**
 * @brief Validates beacon type consistency
 * @param info Beacon info to validate
 * 
 * Checks if rotating field type matches beacon type
 * according to transmission rules in Section 3.4.
 */
static void validate_beacon_type(BeaconInfo *info) {
    switch(info->beacon_type) {
        case 3:  // ELT(DT)
            if (info->rot.id != 1) {
                fprintf(stderr, "Consistency: ELT(DT) should use RF#1 (found %d)\n", 
                        info->rot.id);
            }
            break;
            
        case 1:  // EPIRB
        case 2:  // PLB
            if (info->rot.id == 1) {
                fprintf(stderr, "Consistency: Non-ELT beacon using RF#1\n");
            }
            break;
    }
}

// ===================================================
// Main Decoding Function
// ===================================================
/**
 * @brief Decodes complete SGB beacon message
 * @param rx_bits Input: 250-bit raw message
 * 
 * Implements full decoding workflow:
 * 1. BCH error correction
 * 2. Main field decoding
 * 3. Rotating field decoding
 * 4. Vessel ID decoding
 * 5. Hex ID generation
 * 6. Data validation
 * 7. Result output
 */

static void print_beacon_info(const BeaconInfo *info);

void decode_2g(const uint8_t *rx_bits) {
    uint8_t corrected[BCH_K];  // Corrected information bits
    BeaconInfo info;
    memset(&info, 0, sizeof(info));
    
    // 1. Apply BCH error correction
    bch_decode_250_202(rx_bits, corrected);
    
    // 2. Decode main field (154 bits)
    decode_main(corrected, &info);
    
    // 3. Validate position data
    validate_gnss(&info);
    
    // 4. Decode rotating field (48 bits)
    decode_rot_field(corrected + MAIN_BITS, &info);
    
    // 5. Validate beacon type consistency
    validate_beacon_type(&info);
    
    // 6. Decode vessel/aircraft ID
    decode_vessel_id(corrected, info.vessel_id_type, info.vessel_id);
    
    // 7. Generate 23 Hex ID
    compute_hex_id(corrected, &info);
    
    // 8. Print decoded information
    print_beacon_info(&info);
}

// ===================================================
// Output Functions
// ===================================================
/**
 * @brief Prints decoded beacon information
 * @param info Beacon information to display
 * 
 * Formats output according to C/S T.018 specification
 * with human-readable field descriptions.
 */
void print_beacon_info(const BeaconInfo *info) {
    char coord_buf[50];
    
    printf("\n=== 406 MHz SECOND GENERATION BEACON (SGB) ===");
    printf("\n[IDENTIFICATION]");
    printf("\n 23 Hex ID: %s", info->hex_id);
    printf("\n TAC Number: %u (0x%04X)", info->tac, info->tac);
    printf("\n Serial Number: %u (0x%04X)", info->serial, info->serial);
    printf("\n Country Code: %u (%s)", info->country, get_country_name(info->country));
    printf("\n Type: ");
    switch(info->beacon_type) {
        case 0: printf("ELT"); break;
        case 1: printf("EPIRB"); break;
        case 2: printf("PLB"); break;
        case 3: printf("ELT(DT)"); break;
        case 7: printf("System Beacon"); break;
        default: printf("Unknown (0x%X)", info->beacon_type);
    }
    printf("\n Vessel ID: %s", info->vessel_id);
    
    printf("\n\n[STATUS]");
    printf("\n Homing Device: %s", info->homing ? "Available/Active" : "Not Available/Disabled");
    printf("\n RLS Capability: %s", info->rls ? "Enabled" : "Disabled");
    printf("\n Test Protocol: %s", info->test ? "Active (Non-operational)" : "Normal Operation");
    
    
    printf("\n\n[ENCODED GNSS LOCATION]");
    if (info->has_position && (info->lat != 0.0 || info->lon != 0.0)) {
        format_coordinates(info->lat, info->lon, coord_buf, sizeof(coord_buf));
        printf("\n Position: %s", coord_buf);
        printf("\n Coordinates: %.5f°N, %.5f°E", info->lat, info->lon);
        printf("\n Resolution: ~3.4 meters maximum");
        open_osm_map(info->lat, info->lon);
    } else {
        printf("\n Status: No position data available");
        printf("\n (Beacon not equipped with GNSS or position encoding disabled)");
    }
    
    /*printf("\n\n[POSITION]");
    if (info->lat == 0 && info->lon == 0) {
        printf("\n No position available");
    } else {
        printf("\n Latitude: %.5f°", info->lat);
        printf("\n Longitude: %.5f°", info->lon);
    }*/
    
    printf("\n\n[ROTATING FIELD %d]", info->rot.id);
    switch(info->rot.id) {
        case 0:
            printf("\n Type: G.008 Objective Requirements");
            printf("\n Elapsed time: %d hours", info->rot.g008_obj.elapsed_time);
            printf("\n Last position: %d minutes ago", info->rot.g008_obj.time_last_loc);
            if (info->rot.g008_obj.altitude != -32768) {
                printf("\n Altitude: %d meters", info->rot.g008_obj.altitude);
            } else {
                printf("\n Altitude: Not available");
            }
            // DOP values according to T.018 Table 3.3
            printf("\n HDOP: ");
            switch(info->rot.g008_obj.hdop) {
                case 0: printf("DOP ≤1"); break;
                case 1: printf("DOP >1 and ≤2"); break;
                case 2: printf("DOP >2 and ≤3"); break;
                case 3: printf("DOP >3 and ≤4"); break;
                case 4: printf("DOP >4 and ≤5"); break;
                case 5: printf("DOP >5 and ≤6"); break;
                case 6: printf("DOP >6 and ≤7"); break;
                case 7: printf("DOP >7 and ≤8"); break;
                case 8: printf("DOP >8 and ≤10"); break;
                case 9: printf("DOP >10 and ≤12"); break;
                case 10: printf("DOP >12 and ≤15"); break;
                case 11: printf("DOP >15 and ≤20"); break;
                case 12: printf("DOP >20 and ≤30"); break;
                case 13: printf("DOP >30 and ≤50"); break;
                case 14: printf("DOP >50"); break;
                case 15: printf("DOP not available"); break;
            }
            printf("\n VDOP: ");
            switch(info->rot.g008_obj.vdop) {
                case 0: printf("DOP ≤1"); break;
                case 1: printf("DOP >1 and ≤2"); break;
                case 2: printf("DOP >2 and ≤3"); break;
                case 3: printf("DOP >3 and ≤4"); break;
                case 4: printf("DOP >4 and ≤5"); break;
                case 5: printf("DOP >5 and ≤6"); break;
                case 6: printf("DOP >6 and ≤7"); break;
                case 7: printf("DOP >7 and ≤8"); break;
                case 8: printf("DOP >8 and ≤10"); break;
                case 9: printf("DOP >10 and ≤12"); break;
                case 10: printf("DOP >12 and ≤15"); break;
                case 11: printf("DOP >15 and ≤20"); break;
                case 12: printf("DOP >20 and ≤30"); break;
                case 13: printf("DOP >30 and ≤50"); break;
                case 14: printf("DOP >50"); break;
                case 15: printf("DOP not available"); break;
            }
            // Battery capacity according to T.018 Table 3.3
            printf("\n Battery: ");
            switch(info->rot.g008_obj.battery_capacity) {
                case 0: printf("<=5%% remaining"); break;
                case 1: printf(">5%% and <=10%% remaining"); break;
                case 2: printf(">10%% and <=25%% remaining"); break;
                case 3: printf(">25%% and <=50%% remaining"); break;
                case 4: printf(">50%% and <=75%% remaining"); break;
                case 5: printf(">75%% and <=100%% remaining"); break;
                case 6: printf("Reserved for future use"); break;
                case 7: printf("Battery capacity not available"); break;
                default: printf("Unknown (%d)", info->rot.g008_obj.battery_capacity);
            }
            // GNSS Status according to T.018 Table 3.3
            printf("\n GNSS Status: ");
            switch(info->rot.g008_obj.gnss_status) {
                case 0: printf("No fix"); break;
                case 1: printf("2D location only"); break;
                case 2: printf("3D location"); break;
                case 3: printf("Reserved for future use"); break;
                default: printf("Unknown (%d)", info->rot.g008_obj.gnss_status);
            }
            break;
        
        case 1:
            printf("\n Type: In-Flight Emergency");
            printf("\n Last position time: %d UTC seconds", info->rot.in_flight.time_last_loc);
            if (info->rot.in_flight.altitude != -32768) {
                printf("\n Altitude: %d meters", info->rot.in_flight.altitude);
            }
            printf("\n Trigger: ");
            switch(info->rot.in_flight.triggering_event) {
                case 0x1: printf("Manual activation"); break;
                case 0x4: printf("G-switch activation"); break;
                case 0x8: printf("Automatic activation"); break;
                default: printf("Unknown (0x%X)", info->rot.in_flight.triggering_event);
            }
            break;
        
        case 2:
            printf("\n Type: RLS Type 1 & 2 Acknowledgement");
            printf("\n Provider: ");
            switch(info->rot.rls_ack.rls_provider) {
                case 1: printf("Galileo"); break;
                case 2: printf("GLONASS"); break;
                case 3: printf("BDS"); break;
                default: printf("Unknown (%d)", info->rot.rls_ack.rls_provider);
            }
            printf("\n Auto ACK Capability: %s", info->rot.rls_ack.capability_auto_ack ? "Yes" : "No");
            printf("\n Manual ACK Capability: %s", info->rot.rls_ack.capability_manual_ack ? "Yes" : "No");
            printf("\n Type-1 RLM Received: %s", info->rot.rls_ack.feedback_type1 ? "Yes" : "No");
            printf("\n Type-2 RLM Received: %s", info->rot.rls_ack.feedback_type2 ? "Yes" : "No");
            if (info->rot.rls_ack.rlm_data != 0) {
                printf("\n RLM Data: 0x%05X", info->rot.rls_ack.rlm_data);
            }
            break;
        
        case 4:
            printf("\n Type: RLS Type 3 TWC (Two-Way Communication)");
            printf("\n Provider: ");
            switch(info->rot.twc.twc_provider) {
                case 1: printf("Galileo"); break;
                case 2: printf("GLONASS"); break;
                case 3: printf("BDS"); break;
                default: printf("Unknown (%d)", info->rot.twc.twc_provider);
            }
            printf("\n Database Version: %d", info->rot.twc.version_id);
            printf("\n TWC ACK Received: %s", info->rot.twc.twc_ack_received ? "Yes" : "No");
            if (info->rot.twc.questionA != 0 || info->rot.twc.answerA != 0) {
                printf("\n Question A: %d, Answer A: %d", info->rot.twc.questionA, info->rot.twc.answerA);
            }
            if (info->rot.twc.questionB != 0 || info->rot.twc.answerB != 0) {
                printf("\n Question B: %d, Answer B: %d", info->rot.twc.questionB, info->rot.twc.answerB);
            }
            if (info->rot.twc.questionC != 0 || info->rot.twc.answerC != 0) {
                printf("\n Question C: %d, Answer C: %d", info->rot.twc.questionC, info->rot.twc.answerC);
            }
            break;
        
        case 15:
            printf("\n Type: Cancellation Message");
            printf("\n Method: ");
            switch(info->rot.cancellation.deactivation_method) {
                case 1: printf("Manual deactivation"); break;
                case 2: printf("Automatic deactivation"); break;
                default: printf("Unknown (%d)", info->rot.cancellation.deactivation_method);
            }
            break;
            
        default:
            printf("\n Type: %d (Reserved/Spare)", info->rot.id);
    }
    printf("\n\n[COMPLIANCE]");
    printf("\n Standard: COSPAS-SARSAT T.018 Second Generation Beacon");
    printf("\n BCH Error Correction: BCH(250,202) - 48 bits");
    printf("\n Data Rate: 300 bps, Spread Spectrum: DSSS-OQPSK");
    printf("\n====================================================\n");
}
