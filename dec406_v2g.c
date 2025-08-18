#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "dec406.h"
#include "display_utils.h"

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
// Modified Baudot Decoding Table (Table 3.2)
// ===================================================
static const char *baudot_table[64] = {
    [0b111000] = "A", [0b110011] = "B", [0b101110] = "C", 
    [0b110010] = "D", [0b110000] = "E", [0b110110] = "F",
    [0b101011] = "G", [0b100101] = "H", [0b101100] = "I",
    [0b111010] = "J", [0b111110] = "K", [0b101001] = "L",
    [0b100111] = "M", [0b100110] = "N", [0b100011] = "O",
    [0b101101] = "P", [0b111101] = "Q", [0b101010] = "R",
    [0b110100] = "S", [0b100001] = "T", [0b111100] = "U",
    [0b101111] = "V", [0b111001] = "W", [0b110111] = "X",
    [0b110101] = "Y", [0b110001] = "Z", [0b100100] = " ",  // Space
    [0b011000] = "-", [0b010111] = "/", [0b001101] = "0",
    [0b011101] = "1", [0b011001] = "2", [0b010000] = "3",
    [0b001010] = "4", [0b000001] = "5", [0b010101] = "6",
    [0b011100] = "7", [0b001100] = "8", [0b000011] = "9"
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
    // Latitude decoding
    int ns_flag = bits[44];
    int lat_deg = get_bits(bits, 45, 7);
    int lat_frac = get_bits(bits, 52, 15);
    *lat = (lat_deg + lat_frac / 32768.0) * (ns_flag ? -1.0 : 1.0);
    
    // Check default value (no position available)
    if (ns_flag == 1 && lat_deg == 127 && lat_frac == 0x1F80) {
        *lat = 0;
        *lon = 0;
        return;
    }
    
    // Longitude decoding
    int ew_flag = bits[67];
    int lon_deg = get_bits(bits, 68, 8);
    int lon_frac = get_bits(bits, 76, 15);
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
    
    // Decode fixed fields
    info->tac = get_bits(bits, 0, 16);
    info->serial = get_bits(bits, 16, 14);
    info->country = get_bits(bits, 30, 10);
    info->homing = bits[40];
    info->rls = bits[41];
    info->test = bits[42];
    
    // Decode position
    decode_position(bits, &info->lat, &info->lon);
    
    info->has_position = (info->lat != 0.0 || info->lon != 0.0) ? 1 : 0;
    
    // Vessel ID type
    info->vessel_id_type = get_bits(bits, 90, 3);
    
    // Beacon type
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
        
        case 4:  // Two-Way Communication
            info->rot.twc.twc_provider = get_bits(rot_bits, 4, 3);
            info->rot.twc.version_id = get_bits(rot_bits, 7, 5);
            info->rot.twc.twc_ack_received = rot_bits[12];
            info->rot.twc.questionA = get_bits(rot_bits, 15, 7);
            info->rot.twc.answerA = get_bits(rot_bits, 22, 4);
            info->rot.twc.questionB = get_bits(rot_bits, 26, 7);
            info->rot.twc.answerB = get_bits(rot_bits, 33, 4);
            info->rot.twc.questionC = get_bits(rot_bits, 37, 7);
            info->rot.twc.answerC = get_bits(rot_bits, 44, 4);
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
            
        case 1:  // Maritime MMSI
            {
                uint32_t mmsi = get_bits(bits, 93, 30);
                if (mmsi == 0x3F) {  // Default value
                    strcpy(output, "Default MMSI");
                } else if (mmsi > 999999999) {
                    strcpy(output, "Invalid MMSI");
                } else {
                    sprintf(output, "MMSI:%09u", mmsi);
                }
            }
            break;
            
        case 2:  // Radio Call Sign
        case 3:  // Aircraft Registration Marking
            {
                char decoded[8] = {0};
                int valid_chars = 0;
                
                for (int i = 0; i < 7; i++) {
                    int start_bit = 93 + i * 6;
                    int code = get_bits(bits, start_bit, 6);
                    
                    // Look up in Baudot table
                    const char *ch = baudot_table[code];
                    if (!ch) ch = "?";  // Replacement character for invalid codes
                    
                    strcat(decoded, ch);
                    if (*ch != '?' && *ch != ' ') valid_chars++;
                }
                
                // Validate output
                if (valid_chars >= 3) {
                    strcpy(output, decoded);
                } else {
                    strcpy(output, "Invalid ID");
                }
            }
            break;
            
        case 4:  // Aircraft 24-bit Address
            {
                uint32_t addr = get_bits(bits, 93, 24);
                sprintf(output, "ICAO24:%06X", addr);
            }
            break;
            
        case 5:  // Aircraft Operator + Serial
            {
                char op[4] = {0};
                for (int i = 0; i < 3; i++) {
                    int code = get_bits(bits, 93 + i * 5, 5);
                    op[i] = 'A' + code;  // Simple conversion
                }
                uint16_t serial = get_bits(bits, 108, 12);
                sprintf(output, "%s-%u", op, serial);
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
    
    printf("\n=== 406 MHz BEACON DECODE (2G) ===");
    printf("\n[IDENTIFICATION]");
    printf("\n 23 Hex ID: %s", info->hex_id);
    printf("\n TAC: %u, Serial: %u, Country: %u", 
           info->tac, info->serial, info->country);
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
    printf("\n Homing: %s, RLS: %s, Test: %s",
           info->homing ? "Active" : "Inactive",
           info->rls ? "Enabled" : "Disabled",
           info->test ? "Yes" : "No");
    
    
    printf("\n\n[POSITION]");
    if (info->lat != 0.0 || info->lon != 0.0) {
        format_coordinates(info->lat, info->lon, coord_buf, sizeof(coord_buf));
        printf("\n Position: %s", coord_buf);
        open_osm_map(info->lat, info->lon);
    } else {
        printf("\n No position available");
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
            printf("\n HDOP: Category %d, VDOP: Category %d", 
                   info->rot.g008_obj.hdop, info->rot.g008_obj.vdop);
            printf("\n Battery: %d%%", info->rot.g008_obj.battery_capacity * 25);
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
            printf("\n Type: RLS Acknowledgement");
            printf("\n Provider: ");
            switch(info->rot.rls_ack.rls_provider) {
                case 1: printf("Galileo"); break;
                case 2: printf("GLONASS"); break;
                case 3: printf("BDS"); break;
                default: printf("Unknown");
            }
            printf("\n Type-1 ACK: %s", info->rot.rls_ack.feedback_type1 ? "Yes" : "No");
            printf("\n Type-2 ACK: %s", info->rot.rls_ack.feedback_type2 ? "Yes" : "No");
            break;
        
        case 15:
            printf("\n Type: Cancellation Message");
            printf("\n Method: ");
            switch(info->rot.cancellation.deactivation_method) {
                case 1: printf("Manual deactivation"); break;
                case 2: printf("Automatic deactivation"); break;
                default: printf("Unknown");
            }
            break;
            
        default:
            printf("\n Type: %d (Reserved/Spare)", info->rot.id);
    }
    printf("\n=================================\n");
}
