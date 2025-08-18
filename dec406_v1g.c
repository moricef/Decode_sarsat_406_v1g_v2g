#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "dec406.h"
#include "display_utils.h"

// ===================================================
// Constants and structures
// ===================================================
#define SHORT_FRAME_BITS 112
#define LONG_FRAME_BITS 144

typedef enum {
    PROTOCOL_UNKNOWN,
    PROTOCOL_STANDARD_LOCATION,
    PROTOCOL_NATIONAL_LOCATION,
    PROTOCOL_USER_PROTOCOL,
    PROTOCOL_TEST
} ProtocolType;

typedef struct {
    double lat;
    double lon;
    char vessel_id[64];
    char hex_id[24];
    uint16_t country_code;
    uint32_t serial;
    uint32_t mmsi;
    uint32_t aircraft_address;
    uint32_t operator_designator;
    uint32_t c_s_ta_number;
    uint8_t beacon_type;
    uint8_t id_type;
    uint8_t emergency_code;
    uint8_t auxiliary_device;
    uint8_t test_flag;
    uint8_t homing_flag;
    uint8_t position_source;
    uint8_t has_position;
    ProtocolType protocol;
    uint8_t frame_type;  // 0 = short, 1 = long
    uint8_t crc_error;
} BeaconInfo1G;

static void decode_user_location(const char *s, double *lat, double *lon) {
    *lat = 0.0;
    *lon = 0.0;
}

// ===================================================
// Decoding CRC1 CRC2
// ===================================================
static int test_crc1(const char *s) {
    int g[] = {1,0,0,1,1,0,1,1,0,1,1,0,0,1,1,1,1,0,0,0,1,1};
    int div[22];
    int i, j, ss = 0;
    int zero = 0;

    // Vérifie si le bloc CRC1 est nul
    for (i = 85; i < 106; i++) {
        if (s[i] == '1') zero++;
    }

    i = 24;
    for (j = 0; j < 22; j++) {
        div[j] = (s[i+j] == '1') ? 1 : 0;
    }

    while (i < 85) {
        for (j = 0; j < 22; j++) {
            div[j] = div[j] ^ g[j];
        }
        while ((div[0] == 0) && (i < 85)) {
            for (j = 0; j < 21; j++) {
                div[j] = div[j+1];
            }
            if (i < 84) {
                div[21] = (s[i+22] == '1') ? 1 : 0;
            }
            i++;
        }
    }

    for (j = 0; j < 22; j++) ss += div[j];
    return (ss == 0 || zero == 0) ? 0 : 1;
}

static int test_crc2(const char *s) {
    int g[] = {1,0,1,0,1,0,0,1,1,1,0,0,1};
    int div[13];
    int i, j, ss = 0;
    int zero = 0;

    // Vérifie si le bloc CRC2 est nul
    for (i = 132; i < 144; i++) {
        if (s[i] == '1') zero++;
    }

    i = 106;
    for (j = 0; j < 13; j++) {
        div[j] = (s[i+j] == '1') ? 1 : 0;
    }

    while (i < 132) {
        for (j = 0; j < 13; j++) {
            div[j] = div[j] ^ g[j];
        }
        while ((div[0] == 0) && (i < 132)) {
            for (j = 0; j < 12; j++) {
                div[j] = div[j+1];
            }
            if (i < 131) {
                div[12] = (s[i+13] == '1') ? 1 : 0;
            }
            i++;
        }
    }

    for (j = 0; j < 13; j++) ss += div[j];
    return (ss == 0 || zero == 0) ? 0 : 1;
}

// ===================================================
// Decoding tables
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
    [0b110101] = "Y", [0b110001] = "Z", [0b100100] = " ",
    [0b011000] = "-", [0b010111] = "/", [0b001101] = "0",
    [0b011101] = "1", [0b011001] = "2", [0b010000] = "3",
    [0b001010] = "4", [0b000001] = "5", [0b010101] = "6",
    [0b011100] = "7", [0b001100] = "8", [0b000011] = "9"
};

// ===================================================
// Utility functions
// ===================================================
static uint32_t get_bits(const char *s, int start, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        val = (val << 1) | (s[start + i] == '1' ? 1 : 0);
    }
    return val;
}

static char baudot_decode(int code) {
    return (code >= 0 && code < 64 && baudot_table[code]) ? 
           *baudot_table[code] : '?';  // Replacement character
}

// ===================================================
// Location decoding functions
// ===================================================
static void decode_standard_location(const char *s, double *lat, double *lon, int frame_length) {
    if (frame_length == SHORT_FRAME_BITS) {
        // Décodage spécifique aux trames courtes
        int ns_flag = s[64] == '1';
        int lat_deg = get_bits(s, 65, 7);
        int lat_min = 15 * get_bits(s, 72, 2);
        *lat = (lat_deg + lat_min / 60.0) * (ns_flag ? -1 : 1);
        
        int ew_flag = s[74] == '1';
        int lon_deg = get_bits(s, 75, 8);
        int lon_min = 15 * get_bits(s, 83, 3);
        *lon = (lon_deg + lon_min / 60.0) * (ew_flag ? -1 : 1);
    } else {
        // Décodage standard pour trames longues
        int ns_flag = s[64] == '1';
        int lat_deg = get_bits(s, 65, 7);
        int lat_min = get_bits(s, 72, 6);
        int lat_sec = 4 * get_bits(s, 78, 4);
        *lat = (lat_deg + lat_min / 60.0 + lat_sec / 3600.0) * (ns_flag ? -1 : 1);
        
        int ew_flag = s[84] == '1';
        int lon_deg = get_bits(s, 85, 8);
        int lon_min = get_bits(s, 93, 6);
        int lon_sec = 4 * get_bits(s, 99, 4);
        *lon = (lon_deg + lon_min / 60.0 + lon_sec / 3600.0) * (ew_flag ? -1 : 1);
    }
}

/*static void decode_user_location(const char *s, double *lat, double *lon) {
    *lat = 0.0;
    *lon = 0.0;
}*/

static void decode_national_location(const char *s, double *lat, double *lon, int frame_length) {
    int ns_flag = s[58] == '1';
    int lat_deg = get_bits(s, 59, 7);
    int lat_min = 2 * get_bits(s, 66, 5);
    
    if (frame_length == LONG_FRAME_BITS) {
        int lat_sec = 4 * get_bits(s, 115, 4);
        *lat = (lat_deg + lat_min / 60.0 + lat_sec / 3600.0) * (ns_flag ? -1 : 1);
    } else {
        *lat = (lat_deg + lat_min / 60.0) * (ns_flag ? -1 : 1);
    }
    
    int ew_flag = s[71] == '1';
    int lon_deg = get_bits(s, 72, 8);
    int lon_min = 2 * get_bits(s, 80, 5);
    
    if (frame_length == LONG_FRAME_BITS) {
        int lon_sec = 4 * get_bits(s, 122, 4);
        *lon = (lon_deg + lon_min / 60.0 + lon_sec / 3600.0) * (ew_flag ? -1 : 1);
    } else {
        *lon = (lon_deg + lon_min / 60.0) * (ew_flag ? -1 : 1);
    }
}

// ===================================================
// Identification decoding functions
// ===================================================
static void decode_mmsi(const char *s, BeaconInfo1G *info) {
    // Original implementation
    uint32_t mmsi = 0;
    for (int i = 59; i > 39; i--) {
        if (s[i] == '1') mmsi += (1 << (59 - i));
    }
    info->mmsi = mmsi;
    snprintf(info->vessel_id, sizeof(info->vessel_id), "MMSI:%09u", mmsi);
}

static void decode_aircraft_address(const char *s, BeaconInfo1G *info) {
    // Original implementation
    uint32_t addr = 0;
    for (int i = 63; i > 39; i--) {
        if (s[i] == '1') addr += (1 << (63 - i));
    }
    info->aircraft_address = addr;
    snprintf(info->vessel_id, sizeof(info->vessel_id), "ADDR:%06X", addr);
}

static void decode_baudot_id(const char *s, int start, int count, char *output) {
    // Original implementation
    for (int j = 0; j < count; j++) {
        int pos = start + j * 6;
        int code = get_bits(s, pos, 6);
        output[j] = baudot_decode(code);
    }
    output[count] = '\0';
}

static void decode_aircraft_operator(const char *s, BeaconInfo1G *info) {
    // Original implementation for aircraft operator designator
    uint32_t designator = 0;
    for (int i = 54; i > 39; i--) {
        if (s[i] == '1') designator += (1 << (54 - i));
    }
    info->operator_designator = designator;
    
    char op[4] = {0};
    for (int i = 0; i < 3; i++) {
        op[i] = 'A' + get_bits(s, 93 + i*5, 5);
    }
    uint16_t serial = get_bits(s, 108, 12);
    snprintf(info->vessel_id, sizeof(info->vessel_id), "%s-%u", op, serial);
}

#ifdef ADVANCED_DECODING
static void decode_c_s_ta_number(const char *s, BeaconInfo1G *info) {
    // Original implementation for C/S TA number
    uint32_t ta_num = 0;
    for (int i = 49; i > 39; i--) {
        if (s[i] == '1') ta_num += (1 << (49 - i));
    }
    info->c_s_ta_number = ta_num;
    
    uint32_t serial = get_bits(s, 50, 14);
    info->serial = serial;
    snprintf(info->vessel_id, sizeof(info->vessel_id), "TA:%u-%u", ta_num, serial);
}
#endif

// ===================================================
// Specialized decoding functions
// ===================================================
static void decode_emergency_codes(const char *s, BeaconInfo1G *info) {
    // Original implementation for emergency codes
    if (s[106] == '1') {
        info->emergency_code = get_bits(s, 107, 4);
        
        // Emergency code interpretation
        switch (info->emergency_code) {
            case 1: strcat(info->vessel_id, " FIRE/EXPLOSION"); break;
            case 2: strcat(info->vessel_id, " FLOODING"); break;
            case 3: strcat(info->vessel_id, " COLLISION"); break;
            case 4: strcat(info->vessel_id, " GROUNDING"); break;
            case 5: strcat(info->vessel_id, " LISTING"); break;
            case 6: strcat(info->vessel_id, " SINKING"); break;
            case 7: strcat(info->vessel_id, " DISABLED"); break;
            case 8: strcat(info->vessel_id, " ABANDONING"); break;
            default: strcat(info->vessel_id, " UNKNOWN_EMERG");
        }
    }
}

static void decode_auxiliary_device(const char *s, BeaconInfo1G *info) {
    // Original implementation for auxiliary devices
    int code = get_bits(s, 83, 2);
    info->auxiliary_device = code;
    
    switch (code) {
        case 0: strcat(info->vessel_id, " NO_AUX"); break;
        case 1: strcat(info->vessel_id, " 121.5MHz"); break;
        case 2: strcat(info->vessel_id, " 9GHz_SART"); break;
        case 3: strcat(info->vessel_id, " OTHER_AUX"); break;
    }
}

static void decode_serial_number(const char *s, BeaconInfo1G *info) {
    // Original implementation for serial numbers
    uint32_t serial = 0;
    int start = (info->frame_type == LONG_FRAME_BITS) ? 43 : 40;
    int length = (info->frame_type == LONG_FRAME_BITS) ? 20 : 18;
    
    for (int i = start + length - 1; i >= start; i--) {
        if (s[i] == '1') serial += (1 << (start + length - 1 - i));
    }
    info->serial = serial;
}

#ifdef ADVANCED_DECODING
static void decode_all_zero_or_national_use(const char *s, BeaconInfo1G *info) {
    // Original implementation for all-zero/national use
    uint32_t value = get_bits(s, 63, 10);
    snprintf(info->vessel_id + strlen(info->vessel_id), 
             sizeof(info->vessel_id) - strlen(info->vessel_id),
             " NAT-USE:%03X", value);
}

static void decode_additional_elt_number(const char *s, BeaconInfo1G *info) {
    // Original implementation for additional ELT number
    uint8_t elt_no = get_bits(s, 67, 6);
    snprintf(info->vessel_id + strlen(info->vessel_id), 
             sizeof(info->vessel_id) - strlen(info->vessel_id),
             " ELT:%02u", elt_no);
}

static void decode_operator_designator(const char *s, BeaconInfo1G *info) {
    // Original implementation for operator designator
    char op[4] = {0};
    for (int i = 0; i < 3; i++) {
        op[i] = 'A' + get_bits(s, 43 + i*8, 8);
    }
    info->operator_designator = get_bits(s, 67, 16);
    snprintf(info->vessel_id + strlen(info->vessel_id), 
             sizeof(info->vessel_id) - strlen(info->vessel_id),
             " OP:%s-%04X", op, info->operator_designator);
}

static void decode_c_s_cert_number(const char *s, BeaconInfo1G *info) {
    // Original implementation for C/S cert number
    uint32_t cert_num = get_bits(s, 73, 10);
    snprintf(info->vessel_id + strlen(info->vessel_id), 
             sizeof(info->vessel_id) - strlen(info->vessel_id),
             " CERT:%05u", cert_num);
}
#endif

static void decode_test_data(const char *s, BeaconInfo1G *info) {
    // Original implementation for test data
    char test_data[20] = {0};
    for (int j = 0; j < 6; j++) {
        int i = 39 + j * 8;
        uint8_t byte = get_bits(s, i, 8);
        snprintf(test_data + j*2, sizeof(test_data) - j*2, "%02X", byte);
    }
    strcat(info->vessel_id, " TEST:");
    strncat(info->vessel_id, test_data, sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
}

static void decode_orbitography_data(const char *s, BeaconInfo1G *info) {
    // Original implementation for orbitography data
    char orbit_data[20] = {0};
    for (int j = 0; j < 5; j++) {
        int i = 39 + j * 8;
        uint8_t byte = get_bits(s, i, 8);
        snprintf(orbit_data + j*2, sizeof(orbit_data) - j*2, "%02X", byte);
    }
    strcat(info->vessel_id, " ORBIT:");
    strncat(info->vessel_id, orbit_data, sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
}

static void decode_national_use_data(const char *s, BeaconInfo1G *info) {
    // Original implementation for national use data
    char national_data[20] = {0};
    for (int j = 0; j < 5; j++) {
        int i = 39 + j * 8;
        uint8_t byte = get_bits(s, i, 8);
        snprintf(national_data + j*2, sizeof(national_data) - j*2, "%02X", byte);
    }
    uint8_t extra = get_bits(s, 79, 6);
    snprintf(national_data + 10, sizeof(national_data) - 10, "-%02X", extra);
    strcat(info->vessel_id, " NAT-USE:");
    strncat(info->vessel_id, national_data, sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
}

static void decode_supplementary_data(const char *s, BeaconInfo1G *info) {
    // Original implementation for supplementary data
    if (s[106] == '1') {
        info->homing_flag = 1;
        strcat(info->vessel_id, " HOMING");
    }
    
    if (s[110] == '1') {
        info->position_source = 1;  // Internal source
        strcat(info->vessel_id, " INT-POS");
    } else {
        info->position_source = 0;  // External source
        strcat(info->vessel_id, " EXT-POS");
    }
}

static void determine_frame_type(const char *s, BeaconInfo1G *info) {
    // Bit 24 determines frame type (0 = short, 1 = long)
    info->frame_type = (s[24] == '1') ? LONG_FRAME_BITS : SHORT_FRAME_BITS;
}

static void decode_protocol(const char *s, BeaconInfo1G *info) {
    int protocol_bits = get_bits(s, 36, 4);
    
    if (info->frame_type == SHORT_FRAME_BITS) {
        // Protocols for short frames
        switch (protocol_bits) {
            case 2: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 6: info->protocol = PROTOCOL_USER_PROTOCOL; break;
            case 1: info->protocol = PROTOCOL_USER_PROTOCOL; break;
            case 3: info->protocol = PROTOCOL_USER_PROTOCOL; break;
            case 7: info->protocol = PROTOCOL_TEST; break;
            case 0: info->protocol = PROTOCOL_USER_PROTOCOL; break;
            case 4: info->protocol = PROTOCOL_USER_PROTOCOL; break;
            case 5: info->protocol = PROTOCOL_USER_PROTOCOL; break;
            default: info->protocol = PROTOCOL_UNKNOWN;
        }
    } else {
        // Protocols for long frames
        switch (protocol_bits) {
            case 2: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 3: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 4: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 5: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 6: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 7: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 8: info->protocol = PROTOCOL_NATIONAL_LOCATION; break;
            case 9: info->protocol = PROTOCOL_NATIONAL_LOCATION; break;
            case 10: info->protocol = PROTOCOL_NATIONAL_LOCATION; break;
            case 11: info->protocol = PROTOCOL_NATIONAL_LOCATION; break;
            case 12: info->protocol = PROTOCOL_STANDARD_LOCATION; break;
            case 13: info->protocol = PROTOCOL_UNKNOWN; break;
            case 14: info->protocol = PROTOCOL_TEST; break;
            case 15: info->protocol = PROTOCOL_NATIONAL_LOCATION; break;
            default: info->protocol = PROTOCOL_UNKNOWN;
        }
    }
}

// ===================================================
// Main decoding function
// ===================================================
static void decode_1g_frame(const char *frame, int frame_length, BeaconInfo1G *info) {
    memset(info, 0, sizeof(BeaconInfo1G));
    info->frame_type = frame_length;
    info->crc_error = 0;  // Initialization by default

    // CRC Verification Before Decoding
    int crc1_failed = test_crc1(frame);
    int crc2_failed = 0;
    
    if (frame_length == LONG_FRAME_BITS) {
        crc2_failed = test_crc2(frame);
    }

    if (crc1_failed || crc2_failed) {
        info->crc_error = 1;
        // Optionnal : Stop decoding if CRC fail
        // return;
    }
    
    
    // Step 1: Determine frame type (short/long)
    determine_frame_type(frame, info);
    
    // Step 2: Decode protocol
    decode_protocol(frame, info);
    
    // Step 3: Decode location based on protocol
    switch (info->protocol) {
        case PROTOCOL_STANDARD_LOCATION:
            decode_standard_location(frame, &info->lat, &info->lon, frame_length);
            break;
            
        case PROTOCOL_NATIONAL_LOCATION:
            decode_national_location(frame, &info->lat, &info->lon, frame_length);
            break;
            
        case PROTOCOL_USER_PROTOCOL:
            decode_user_location(frame, &info->lat, &info->lon);
            break;
            
        default:
            // No location data
            info->lat = 0;
            info->lon = 0;
    }
    
    // Step 4: Decode identification type
    info->id_type = get_bits(frame, 90, 3);
    switch (info->id_type) {
        case 1: 
            decode_mmsi(frame, info);
            break;
            
        case 2: 
        case 3: 
            decode_baudot_id(frame, 93, (frame_length == LONG_FRAME_BITS) ? 7 : 6, 
                           info->vessel_id);
            break;
            
        case 4: 
            decode_aircraft_address(frame, info);
            break;
            
        case 5: 
            decode_aircraft_operator(frame, info);
            break;
            
        case 0:
        default:
            strcpy(info->vessel_id, "ID-NOT-AVAIL");
    }
    
    // Step 5: Decode supplementary data
    decode_supplementary_data(frame, info);
    
    // Step 6: Protocol-specific decoding
    if (info->protocol == PROTOCOL_STANDARD_LOCATION) {
        if (frame_length == LONG_FRAME_BITS) {
            decode_emergency_codes(frame, info);
            decode_auxiliary_device(frame, info);
        }
    }
    else if (info->protocol == PROTOCOL_USER_PROTOCOL) {
        switch (get_bits(frame, 36, 4)) {
            case 2:
            case 6:
                // EPIRB MMSI/Radio or Radio Call Sign
                break;
                
            case 1:
                // ELT Aviation
                break;
                
            case 3:
                // Serial User
                decode_serial_number(frame, info);
                break;
                
            case 7:
                decode_test_data(frame, info);
                break;
                
            case 0:
                decode_orbitography_data(frame, info);
                break;
                
            case 4:
                decode_national_use_data(frame, info);
                break;
        }
    }
    else if (info->protocol == PROTOCOL_TEST) {
        decode_test_data(frame, info);
    }
    
    // Step 7: Decode country code and serial
    info->country_code = get_bits(frame, 26, 10);
    
    // Step 8: Specialized serial number decoding
    if (info->id_type == 5 || info->protocol == PROTOCOL_USER_PROTOCOL) {
        decode_serial_number(frame, info);
    }
    else {
        // Default serial decoding
        int start = (frame_length == LONG_FRAME_BITS) ? 16 : 16;
        int length = (frame_length == LONG_FRAME_BITS) ? 20 : 18;
        info->serial = get_bits(frame, start, length);
    }
    
    // Step 9: Generate HEX ID
    snprintf(info->hex_id, sizeof(info->hex_id), "%s-%04X-%08X",
             (frame_length == LONG_FRAME_BITS) ? "LG" : "SH",
             info->country_code, info->serial);
}

// ===================================================
// Interface function
// ===================================================
void decode_1g(const uint8_t *bits, int length) {
    // Validate frame length
    if (length != SHORT_FRAME_BITS && length != LONG_FRAME_BITS) {
        fprintf(stderr, "Invalid frame length: %d bits\n", length);
        return;
    }

    // Convert bits to string
    char frame_str[LONG_FRAME_BITS + 1];
    for (int i = 0; i < length; i++) {
        frame_str[i] = (bits[i] & 1) ? '1' : '0';
    }
    frame_str[length] = '\0';

    BeaconInfo1G info;
    decode_1g_frame(frame_str, length, &info);
    
    // Display results
    char coord_buf[100] = "Position not available";
    if (info.lat != 0 || info.lon != 0) {
        format_coordinates(info.lat, info.lon, coord_buf, sizeof(coord_buf));
    }
    
    if (info.crc_error) {
        printf("\n!!! CRC ERROR - DATA MAY BE CORRUPTED !!!");
    }
    
    printf("\n=== 406 MHz BEACON DECODE (1G %s) ===", 
           (length == LONG_FRAME_BITS) ? "LONG" : "SHORT");
    printf("\nProtocol: %d", info.protocol);
    printf("\nCountry: %u", info.country_code);
    printf("\nSerial: %u", info.serial);
    printf("\nID Type: %u", info.id_type);
    printf("\nIdentification: %s", info.vessel_id);
    printf("\nPosition: %s", coord_buf);
    
    if (length == LONG_FRAME_BITS) {
        if (info.emergency_code) {
            printf("\nEmergency code: %u", info.emergency_code);
        }
        if (info.auxiliary_device) {
            printf("\nAuxiliary device: %u", info.auxiliary_device);
        }
        if (info.homing_flag) {
            printf("\n121.5 MHz Homing: Active");
        }
        printf("\nPosition source: %s", info.position_source ? "Internal" : "External");
    }
    
    printf("\nHEX ID: %s\n", info.hex_id);
    
    if (info.lat != 0 || info.lon != 0) {
        open_osm_map(info.lat, info.lon);
    }

    log_to_terminal("1G decoding successful");
}
