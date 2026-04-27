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
#include <math.h>
#include <stdlib.h>
#include "dec406.h"
#include "display_utils.h"

// ===================================================
// Constants and structures
// ===================================================
#define SHORT_FRAME_BITS FRAME_1G_SHORT
#define LONG_FRAME_BITS FRAME_1G_LONG

typedef enum {
    PROTOCOL_UNKNOWN,
    PROTOCOL_STANDARD_LOCATION,
    PROTOCOL_NATIONAL_LOCATION,
    PROTOCOL_USER_PROTOCOL,
    PROTOCOL_TEST,
    PROTOCOL_EMERGENCY_ELT,
    PROTOCOL_EMERGENCY_EPIRB,
    PROTOCOL_EMERGENCY_PLB,
    PROTOCOL_RLS_LOCATION,
    PROTOCOL_SHIP_SECURITY
} ProtocolType;

typedef struct {
    double lat;
    double lon;
    double base_lat;   // Base latitude from PDF-1
    double base_lon;   // Base longitude from PDF-1
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
    uint8_t frame_type;
    uint8_t crc_error;
    uint8_t activation_method;
    uint8_t location_freshness;
    // Fields for position offsets
    int lat_offset_sign;     // Latitude offset sign (1 = positive, -1 = negative)
    int lon_offset_sign;     // Longitude offset sign (1 = positive, -1 = negative)
    uint8_t lat_offset_min;  // Latitude offset min (0-15)
    uint8_t lat_offset_sec;  // Latitude offset sec (0-56 in 4-second resolution)
    uint8_t lon_offset_min;  // Longitude offset min (0-15)
    uint8_t lon_offset_sec;  // Longitude offset sec (0-56 in 4-second resolution)
    uint8_t protocol_bits;
} BeaconInfo1G;

// Forward declarations for all decode functions
static void decode_user_location(const char *s, BeaconInfo1G *info, int frame_length);
static void decode_standard_location(const char *s, BeaconInfo1G *info, int frame_length);
static void decode_national_location(const char *s, BeaconInfo1G *info, int frame_length);
static void decode_elt_dt_location(const char *s, BeaconInfo1G *info);
static void decode_rls_location(const char *s, BeaconInfo1G *info, int frame_length);
static void decode_serial_user_protocol(const char *s, BeaconInfo1G *info);
static void decode_user_identification(const char *frame, BeaconInfo1G *info);
static void decode_aircraft_address(const char *s, BeaconInfo1G *info);
static void decode_supplementary_data(const char *s, BeaconInfo1G *info);
static void decode_orbitography_data(const char *bits, BeaconInfo1G *info);
static void decode_standard_test_data(const char *bits, BeaconInfo1G *info);
static void decode_test_beacon_data(const char *bits, BeaconInfo1G *info);
static void decode_national_use_data(const char *bits, BeaconInfo1G *info);
static void decode_radio_callsign_data(const char *bits, BeaconInfo1G *info);
static void display_baudot_42(const char *bits);
static void display_baudot_2(const char *bits);
static void display_specific_beacon(const char *bits);
static char decode_baudot_char(int x);
static int validate_coordinates(double lat, double lon);
static int validate_frame_sync(const char *frame, int frame_length);
static void decode_1g_frame(const char *frame, int frame_length, BeaconInfo1G *info);

// ===================================================
// CRC validation functions
// ===================================================
static int test_crc1(const char *s) {
    int g[] = {1,0,0,1,1,0,1,1,0,1,1,0,0,1,1,1,1,0,0,0,1,1};
    int div[22];
    int i, j, ss = 0;
    int zero = 0;

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
// Utility functions
// ===================================================
static uint32_t get_bits(const char *s, int start, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        val = (val << 1) | (s[start + i] == '1' ? 1 : 0);
    }
    return val;
}

static int validate_coordinates(double lat, double lon) {
    return (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0);
}

// ===================================================
// ELT-DT Location Protocol decoder (Protocol 9) - T.001 Specification
// ===================================================
static void decode_elt_dt_location(const char *s, BeaconInfo1G *info) {
    if (info->frame_type == LONG_FRAME_BITS) {
        // Decode base position (PDF-1)
        uint8_t ns_flag = (s[66] == '1');  // Bit 67 (index 66) - N/S flag
        uint8_t lat_val = get_bits(s, 67, 8);  // Bits 68-75 (indices 67-74) - Latitude value
        info->base_lat = lat_val * 0.5;  // 0.5deg resolution
        if (ns_flag) info->base_lat = -info->base_lat;  // Apply South sign

        uint8_t ew_flag = (s[75] == '1');  // Bit 76 (index 75) - E/W flag
        uint16_t lon_val = get_bits(s, 76, 9);  // Bits 77-85 (indices 76-84) - Longitude value
        info->base_lon = lon_val * 0.5;  // 0.5deg resolution
        if (ew_flag) info->base_lon = -info->base_lon;  // Apply West sign

        // Initial composite position = base position
        info->lat = info->base_lat;
        info->lon = info->base_lon;

        // Activation method (bits 107-108)
        info->activation_method = get_bits(s, 106, 2);
        
        // Altitude (bits 109-112)
        info->auxiliary_device = get_bits(s, 108, 4);
        
        // Location freshness (bits 113-114)
        info->location_freshness = get_bits(s, 112, 2);
        
        // Apply position offsets if available
        if (info->location_freshness > 0) {
            // Latitude offset (bits 115-123 selon spec = indices 114-122 dans le tableau)
            info->lat_offset_sign = (s[114] == '1') ? 1 : -1;  // Bit 115 -> index 114
            info->lat_offset_min = get_bits(s, 115, 4);  // Bits 116-119 -> indices 115-118
            info->lat_offset_sec = get_bits(s, 119, 4) * 4;  // Bits 120-123 -> indices 119-122
    
            // Convert offset to degrees
            double lat_offset = info->lat_offset_sign * 
                            (info->lat_offset_min / 60.0 + info->lat_offset_sec / 3600.0);
            info->lat += lat_offset;
    
            // Longitude offset (bits 124-132 selon spec = indices 123-131 dans le tableau)
            info->lon_offset_sign = (s[123] == '1') ? 1 : -1;  // Bit 124 -> index 123
            info->lon_offset_min = get_bits(s, 124, 4);  // Bits 125-128 -> indices 124-127
            info->lon_offset_sec = get_bits(s, 128, 4) * 4;  // Bits 129-132 -> indices 128-131
    
            // Convert offset to degrees
            double lon_offset = info->lon_offset_sign * 
                            (info->lon_offset_min / 60.0 + info->lon_offset_sec / 3600.0);
            info->lon += lon_offset;
        }
    } else {
        // Short frame handling
        int lat_raw = get_bits(s, 67, 8);
        info->lat = lat_raw / 2.0;
        info->base_lat = info->lat;
        
        int lon_raw = get_bits(s, 75, 9);
        info->lon = lon_raw / 2.0;
        info->base_lon = info->lon;
    }
    
    // Validate final coordinates
    if (!validate_coordinates(info->lat, info->lon)) {
        printf("Warning: Invalid ELT-DT coordinates (%.5f, %.5f)\n", 
               info->lat, info->lon);
        info->lat = 0.0;
        info->lon = 0.0;
    }
}


// ===================================================
// Standard Location Protocol decoder (A3.3.5)
// ===================================================
static void decode_standard_location(const char *s, BeaconInfo1G *info, int frame_length) {
    // Store the protocol type
    info->protocol = PROTOCOL_STANDARD_LOCATION;
    info->has_position = 1;

    // Decode identification data (bits 41-64)
    uint32_t id_data = get_bits(s, 40, 24); // bits 41-64 (indices 40-63)
    
    // Decode based on protocol code (bits 37-40)
    switch (info->protocol_bits) {
        case 0b0010: // MMSI with beacon number
            info->mmsi = (id_data >> 4) & 0xFFFFF; // Last 6 digits of MMSI (20 bits)
            info->serial = id_data & 0xF; // 4-bit beacon number (0-15)
            snprintf(info->vessel_id, sizeof(info->vessel_id), 
                    "MMSI: %09u, Beacon: %u", info->mmsi, info->serial);
            break;
            
        case 0b0011: // 24-bit aircraft address
            info->aircraft_address = id_data;
            snprintf(info->vessel_id, sizeof(info->vessel_id), 
                    "Aircraft Address: %06X", info->aircraft_address);
            break;
            
        case 0b0100: // ELT with serial number (Cospas-Sarsat type approval)
        case 0b0110: // EPIRB with serial number
        case 0b0111: // PLB with serial number
            info->c_s_ta_number = (id_data >> 14) & 0x3FF; // 10-bit type approval (bits 41-50)
            info->serial = id_data & 0x3FFF; // 14-bit serial number (bits 51-64)
            snprintf(info->vessel_id, sizeof(info->vessel_id), 
                    "Type Approval: %u, Serial: %u", info->c_s_ta_number, info->serial);
            break;
            
        case 0b0101: // ELT with aircraft operator designator
            info->operator_designator = (id_data >> 9) & 0x7FFF; // 15-bit operator designator (bits 41-55)
            info->serial = id_data & 0x1FF; // 9-bit serial number (bits 56-64)
            // TODO: Decode operator designator from 15-bit code to 3-letter code
            snprintf(info->vessel_id, sizeof(info->vessel_id), 
                    "Operator: %05X, Serial: %u", info->operator_designator, info->serial);
            break;
            
        case 0b1100: // MMSI with spare bits
            info->mmsi = (id_data >> 4) & 0xFFFFF; // Last 6 digits of MMSI (20 bits)
            // Bits 61-64 are spare and set to "0000"
            snprintf(info->vessel_id, sizeof(info->vessel_id), 
                    "MMSI: %09u", info->mmsi);
            break;
            
        default:
            snprintf(info->vessel_id, sizeof(info->vessel_id), 
                    "Unknown ID Format: %06X", id_data);
            break;
    }

    // Decode base position from PDF-1 (bits 65-85)
    // Latitude (bits 65-74)
    int ns_flag = (s[64] == '1'); // Bit 65: N/S flag (N=0, S=1)
    int lat_quarters = get_bits(s, 65, 9); // Bits 66-74: degrees in 1/4 degree increments
    info->base_lat = lat_quarters * 0.25; // Convert to degrees
    if (ns_flag) info->base_lat = -info->base_lat; // Apply South sign
    
    // Longitude (bits 75-85)
    int ew_flag = (s[74] == '1'); // Bit 75: E/W flag (E=0, W=1)
    int lon_quarters = get_bits(s, 75, 10); // Bits 76-85: degrees in 1/4 degree increments
    info->base_lon = lon_quarters * 0.25; // Convert to degrees
    if (ew_flag) info->base_lon = -info->base_lon; // Apply West sign
    
    // Initial composite position = base position
    info->lat = info->base_lat;
    info->lon = info->base_lon;
    
    // Initialize offset fields to default values
    info->lat_offset_sign = 0;
    info->lat_offset_min = 0;
    info->lat_offset_sec = 0;
    info->lon_offset_sign = 0;
    info->lon_offset_min = 0;
    info->lon_offset_sec = 0;
    
    // For long frames, decode PDF-2 offsets
    if (frame_length == LONG_FRAME_BITS) {
        // Check fixed bits (bits 107-110 should be "1101")
        int fixed_bits = get_bits(s, 106, 4);
        if (fixed_bits != 0b1101) {
            printf("Warning: Invalid fixed bits in PDF-2: %04b\n", fixed_bits);
        }
        
        // Position data source (bit 111)
        info->position_source = (s[110] == '1') ? 1 : 0; // 1=internal, 0=external
        
        // 121.5 MHz homing device (bit 112)
        info->homing_flag = (s[111] == '1') ? 1 : 0;
        
        // Δ latitude (bits 113-122)
        info->lat_offset_sign = (s[112] == '1') ? 1 : -1; // Bit 113: sign (0=minus, 1=plus)
        info->lat_offset_min = get_bits(s, 113, 5); // Bits 114-118: minutes (0-30)
        info->lat_offset_sec = get_bits(s, 118, 4) * 4; // Bits 119-122: seconds in 4-second increments
        
        // Δ longitude (bits 123-132)
        info->lon_offset_sign = (s[122] == '1') ? 1 : -1; // Bit 123: sign (0=minus, 1=plus)
        info->lon_offset_min = get_bits(s, 123, 5); // Bits 124-128: minutes (0-30)
        info->lon_offset_sec = get_bits(s, 128, 4) * 4; // Bits 129-132: seconds in 4-second increments
        
        // Apply offsets to calculate composite position
        double lat_offset = info->lat_offset_sign * 
                          (info->lat_offset_min / 60.0 + info->lat_offset_sec / 3600.0);
        double lon_offset = info->lon_offset_sign * 
                          (info->lon_offset_min / 60.0 + info->lon_offset_sec / 3600.0);
        
        info->lat += lat_offset;
        info->lon += lon_offset;
    }
    
    // Validate final coordinates
    if (!validate_coordinates(info->lat, info->lon)) {
        printf("Warning: Invalid Standard Location coordinates (%.5f, %.5f)\n", 
               info->lat, info->lon);
        info->lat = 0.0;
        info->lon = 0.0;
        info->has_position = 0;
    }
    
    // Generate hex ID
    snprintf(info->hex_id, sizeof(info->hex_id), "%s-STD-%04X-%08X",
             (frame_length == LONG_FRAME_BITS) ? "LG" : "SH",
             info->country_code, info->serial);
}

// ===================================================
// User Location Protocol decoder
// ===================================================
// Main function to decode User-Location position data
static void decode_user_location(const char *s, BeaconInfo1G *info, int frame_length) {
    if (frame_length == LONG_FRAME_BITS) {
        // For User-Location Protocol with long message, decode position from PDF-2
        // According to C/S T.001 section A3.2

        // Get user protocol code to check if it's orbitography
        int user_protocol_code = get_bits(s, 36, 3);

        // If it's orbitography (code 000), don't decode position
        // has_position was already set to 0 by decode_orbitography_data()
        if (user_protocol_code == 0b000) {
            return;
        }

        // Bit 107: Position source (0=external, 1=internal navigation device)
        info->position_source = (s[106] == '1') ? 1 : 0;

        // Bit 108: N/S flag (N=0, S=1)
        int lat_sign = (s[107] == '0') ? 1 : -1;  // Bit 108 (index 107)

        // Bits 109-115: Latitude degrees (0-90) - 7 bits
        int lat_deg = get_bits(s, 108, 7);  // Bits 109-115 (indices 108-114)

        // Bits 116-119: Latitude minutes/4 (0-14, representing 0-56 minutes in 4-minute steps) - 4 bits
        int lat_min_div4 = get_bits(s, 115, 4); // Bits 116-119 (indices 115-118)

        // Bit 120: E/W flag (E=0, W=1)
        int lon_sign = (s[119] == '0') ? 1 : -1;  // Bit 120 (index 119)

        // Bits 121-128: Longitude degrees (0-180) - 8 bits
        int lon_deg = get_bits(s, 120, 8);  // Bits 121-128 (indices 120-127)

        // Bits 129-132: Longitude minutes/4 (0-14, representing 0-56 minutes in 4-minute steps) - 4 bits
        int lon_min_div4 = get_bits(s, 128, 4); // Bits 129-132 (indices 128-131)

        // Calculate actual position
        // The minutes value is in 4-minute increments, so multiply by 4
        double lat_minutes = lat_min_div4 * 4.0;
        double lon_minutes = lon_min_div4 * 4.0;

        info->lat = lat_sign * (lat_deg + lat_minutes / 60.0);
        info->lon = lon_sign * (lon_deg + lon_minutes / 60.0);

        // Store base position (same as final for User-Location)
        info->base_lat = info->lat;
        info->base_lon = info->lon;

        // Mark that we have position data
        info->has_position = 1;
    } else {
        // Short frame - no position data available for User protocols
        info->has_position = 0;
        info->lat = 0.0;
        info->lon = 0.0;
    }
    
    // Validate final coordinates
    if (info->has_position && !validate_coordinates(info->lat, info->lon)) {
        printf("Warning: Invalid User-Location coordinates (%.5f, %.5f)\n", 
               info->lat, info->lon);
        info->lat = 0.0;
        info->lon = 0.0;
        info->has_position = 0;
    }
}

// Function to decode Serial User Protocol identification
// For protocol code 011 (Serial User)
static void decode_serial_user_protocol(const char *s, BeaconInfo1G *info) {
    // Get the 3-bit protocol code (bits 37-39)
    int protocol_code = get_bits(s, 36, 3);
    
    if (protocol_code != 0b011) {
        // Not a serial user protocol
        return;
    }
    
    // Bits 40-42: Beacon type
    int beacon_type = get_bits(s, 39, 3);  // Bits 40-42 (indices 39-41)
    
    // Bit 43: C/S Type Approval Certificate flag
    int cs_flag = (s[42] == '1') ? 1 : 0;  // Bit 43 (index 42)
    
    // Bits 44-63: Serial number (20 bits)
    uint32_t serial_number = get_bits(s, 43, 20);  // Bits 44-63 (indices 43-62)
    
    // Bits 64-73: All 0s or national use (10 bits)
    uint32_t national_use = get_bits(s, 63, 10);  // Bits 64-73 (indices 63-72)
    
    // Bits 74-83: C/S certificate number or national use (10 bits)
    uint32_t cs_cert_number = get_bits(s, 73, 10);  // Bits 74-83 (indices 73-82)
    
    // Build identification string based on beacon type
    const char *beacon_type_str;
    switch (beacon_type) {
        case 0b000:  // ELT with serial number
            beacon_type_str = "ELT";
            break;
        case 0b001:  // ELT with aircraft operator designator
            beacon_type_str = "ELT (operator)";
            break;
        case 0b010:  // Float free EPIRB
            beacon_type_str = "Float free EPIRB";
            break;
        case 0b011:  // ELT with 24-bit aircraft address
            beacon_type_str = "ELT (24-bit addr)";
            break;
        case 0b100:  // Non-float free EPIRB
            beacon_type_str = "Non-float free EPIRB";
            break;
        case 0b110:  // PLB (Personal Locator Beacon)
            beacon_type_str = "PLB";
            break;
        default:
            beacon_type_str = "Unknown beacon type";
            break;
    }
    
    // Format the identification string
    snprintf(info->vessel_id, sizeof(info->vessel_id), 
             "%s - Serial: %u", beacon_type_str, serial_number);
    
    // Add additional information if available
    if (cs_flag && cs_cert_number > 0) {
        char temp[50];
        snprintf(temp, sizeof(temp), ", C/S Cert: %u", cs_cert_number);
        strncat(info->vessel_id, temp, 
                sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
    }
    
    if (national_use > 0) {
        char temp[50];
        snprintf(temp, sizeof(temp), ", National: %u", national_use);
        strncat(info->vessel_id, temp, 
                sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
    }
    
    // Store the serial number in the info structure
    info->serial = serial_number;
}

// Function to decode user identification for User and User-Location protocols
static void decode_user_identification(const char *frame, BeaconInfo1G *info) {
    // Get the user protocol code (bits 37-39)
    int user_protocol_code = get_bits(frame, 36, 3);
    
    switch (user_protocol_code) {
        case 0b000:  // Orbitography Protocol
            decode_orbitography_data(frame, info);
            strcpy(info->vessel_id, "Orbitography");
            break;
        case 0b001:  // Aviation User Protocol
            display_baudot_2(frame);
            strcpy(info->vessel_id, "Aviation User");
            break;
        case 0b010:  // Maritime User Protocol (MMSI or Radio Call Sign)
            display_baudot_42(frame);
            display_specific_beacon(frame);
            strcpy(info->vessel_id, "Maritime User");
            break;
        case 0b011:  // Serial User Protocol
            decode_serial_user_protocol(frame, info);
            break;
        case 0b100:  // National User Protocol
            decode_national_use_data(frame, info);
            strcpy(info->vessel_id, "National User");
            break;
        case 0b110:  // Radio Call Sign User Protocol
            decode_radio_callsign_data(frame, info);
            strcpy(info->vessel_id, "Radio Call Sign");
            break;
        case 0b111:  // Test User Protocol
            decode_test_beacon_data(frame, info);
            strcpy(info->vessel_id, "Test User");
            break;
        default:
            strcpy(info->vessel_id, "Unknown User Protocol");
            break;
    }
}

// ===================================================
// National Location Protocol decoder
// ===================================================
static void decode_national_location(const char *s, BeaconInfo1G *info, int frame_length) {
    // Decode National ID (bits 41-58 = 18 bits)
    // These are indices 40-57 in the 0-indexed array
    uint32_t national_id = get_bits(s, 40, 18);
    
    // Determine beacon type based on protocol code
    const char *beacon_type_str;
    switch (info->protocol_bits) {
        case 8:   // 1000 = ELT National
            beacon_type_str = "ELT";
            break;
        case 10:  // 1010 = EPIRB National
            beacon_type_str = "EPIRB";
            break;
        case 11:  // 1011 = PLB National
            beacon_type_str = "PLB";
            break;
        case 15:  // 1111 = Test
            beacon_type_str = "TEST";
            break;
        default:
            beacon_type_str = "Unknown";
    }
    
    // Store the identification in vessel_id
    snprintf(info->vessel_id, sizeof(info->vessel_id), 
             "%s National ID: %u", beacon_type_str, national_id);
    
    // Also store the raw ID for hex ID generation
    info->serial = national_id;
    
    // PDF-1: Base position with 2-minute resolution
    // Bits 59-71: Latitude (13 bits)
    int ns_flag = (s[58] == '1');  // Bit 59: N/S flag (N=0, S=1)
    int lat_deg = get_bits(s, 59, 7);  // Bits 60-66: degrees (0-90)
    int lat_min = get_bits(s, 66, 5) * 2;  // Bits 67-71: minutes (0-58) in 2-min increments
    
    // Bits 72-85: Longitude (14 bits)
    int ew_flag = (s[71] == '1');  // Bit 72: E/W flag (E=0, W=1)
    int lon_deg = get_bits(s, 72, 8);  // Bits 73-80: degrees (0-180)
    int lon_min = get_bits(s, 80, 5) * 2;  // Bits 81-85: minutes (0-58) in 2-min increments
    
    // Calculate and store base position
    info->base_lat = lat_deg + lat_min / 60.0;
    if (ns_flag) info->base_lat = -info->base_lat;  // Apply South sign
    
    info->base_lon = lon_deg + lon_min / 60.0;
    if (ew_flag) info->base_lon = -info->base_lon;  // Apply West sign
    
    // Initial composite position = base position
    info->lat = info->base_lat;
    info->lon = info->base_lon;
    
    // Initialize offset fields to default values
    info->lat_offset_sign = 0;
    info->lat_offset_min = 0;
    info->lat_offset_sec = 0;
    info->lon_offset_sign = 0;
    info->lon_offset_min = 0;
    info->lon_offset_sec = 0;
    
    // PDF-2: Position offsets (if long frame and bit 110 = 1)
    if (frame_length == LONG_FRAME_BITS) {
        // Check if position offset data is present (bit 110)
        int additional_data_flag = (s[109] == '1');  // Bit 110: 1 = position offset data
        
        if (additional_data_flag) {
            // Store position data source (bit 111)
            info->position_source = (s[110] == '1') ? 1 : 0;  // 1=internal, 0=external
            
            // Store 121.5 MHz homing device flag (bit 112)
            info->homing_flag = (s[111] == '1') ? 1 : 0;
            
            // Latitude offset (bits 113-119 in doc = indices 112-118 in code)
            info->lat_offset_sign = (s[112] == '1') ? 1 : -1;  // Bit 113 (index 112): sign
            info->lat_offset_min = get_bits(s, 113, 2);  // Bits 114-115: minutes (0-3)
            info->lat_offset_sec = get_bits(s, 115, 4) * 4;  // Bits 116-119: seconds in 4-sec increments
            
            // Longitude offset (bits 120-126 in doc = indices 119-125 in code)
            info->lon_offset_sign = (s[119] == '1') ? 1 : -1;  // Bit 120 (index 119): sign
            info->lon_offset_min = get_bits(s, 120, 2);  // Bits 121-122: minutes (0-3)
            info->lon_offset_sec = get_bits(s, 122, 4) * 4;  // Bits 123-126: seconds in 4-sec increments
            
            // Apply offsets to calculate composite position
            double lat_offset = info->lat_offset_sign * 
                              (info->lat_offset_min / 60.0 + info->lat_offset_sec / 3600.0);
            double lon_offset = info->lon_offset_sign * 
                              (info->lon_offset_min / 60.0 + info->lon_offset_sec / 3600.0);
            
            info->lat += lat_offset;
            info->lon += lon_offset;
        }
        
        // Decode national use bits (127-132) if needed
        // These 6 bits are reserved for national use (additional beacon type identification or other)
        // uint8_t national_use = get_bits(s, 126, 6);  // Bits 127-132 (indices 126-131)
        // Could be used for additional beacon identification if needed
    }
    
    // Validate final coordinates
    if (!validate_coordinates(info->lat, info->lon)) {
        printf("Warning: Invalid National Location coordinates (%.5f, %.5f)\n", info->lat, info->lon);
        info->lat = 0.0;
        info->lon = 0.0;
    }
}

// ===================================================
// RLS Location Protocol decoder
// ===================================================
static void decode_rls_location(const char *s, BeaconInfo1G *info, int frame_length) {
    // RLS Location Protocol structure (bits selon T.001)
    // Bits 41-42: Beacon type (00=ELT, 01=EPIRB, 10=PLB, 11=Test)
    uint8_t beacon_type = get_bits(s, 40, 2);
    
    // Bits 43-46: Check if MMSI encoded (1111) or TAC/National RLS
    uint8_t mmsi_flag = get_bits(s, 42, 4);
    
    if (mmsi_flag == 0xF) {  // MMSI encoding
        // Bits 47-66: Last 6 digits of MMSI
        uint32_t mmsi_last6 = get_bits(s, 46, 20);
        snprintf(info->vessel_id, sizeof(info->vessel_id), 
                 "RLS MMSI: %06u", mmsi_last6);
    } else {
        // Bits 43-52: TAC or National RLS Number
        uint16_t tac = get_bits(s, 42, 10);
        // Bits 53-66: Serial number
        uint16_t serial = get_bits(s, 52, 14);
        
        const char *type_str[] = {"ELT", "EPIRB", "PLB", "TEST"};
        snprintf(info->vessel_id, sizeof(info->vessel_id),
                 "RLS %s TAC:%u Serial:%u", 
                 type_str[beacon_type], tac + (beacon_type == 0 ? 2000 : 
                                               beacon_type == 1 ? 1000 : 3000),
                 serial);
    }
    
    // Position data (30 minute resolution)
    // Bits 67-75: Latitude (9 bits)
    uint8_t ns_flag = (s[66] == '1');  // Bit 67: N/S flag
    uint8_t lat_half_deg = get_bits(s, 67, 8);  // 0.5 degree increments
    info->base_lat = lat_half_deg * 0.5;
    if (ns_flag) info->base_lat = -info->base_lat;
    
    // Bits 76-85: Longitude (10 bits)
    uint8_t ew_flag = (s[75] == '1');  // Bit 76: E/W flag
    uint16_t lon_half_deg = get_bits(s, 76, 9);  // 0.5 degree increments
    info->base_lon = lon_half_deg * 0.5;
    if (ew_flag) info->base_lon = -info->base_lon;
    
    // Initial position = base position
    info->lat = info->base_lat;
    info->lon = info->base_lon;
    
    // PDF-2: Position offsets (if long frame)
    if (frame_length == LONG_FRAME_BITS) {
        // Bits 115-132: Position offsets (18 bits total)
        // Similar structure to other location protocols
        // Implementation depends on specific RLS requirements
    }
}

// ===================================================
// Identification decoding functions
// ===================================================
static void decode_aircraft_address(const char *s, BeaconInfo1G *info) {
    // T.001: bits 43-66 for aircraft 24-bit address (positions 42-65 in 0-indexed)
    uint32_t addr = get_bits(s, 42, 24);
    info->aircraft_address = addr;
    snprintf(info->vessel_id, sizeof(info->vessel_id), "Aircraft %06X", addr);
}

static void decode_supplementary_data(const char *s, BeaconInfo1G *info) {
    (void)s;
    if (info->frame_type == LONG_FRAME_BITS && info->protocol == PROTOCOL_EMERGENCY_ELT) {
        // Buffer temporaire pour éviter les débordements
        char temp_str[128];
        
        // For ELT-DT protocol, decode activation method
        const char* activation_str[] = {
            "manual activation",
            "automatic activation by G-switch",
            "automatic activation by external means",
            "spare"
        };
        
        if (info->activation_method <= 3) {
            snprintf(temp_str, sizeof(temp_str), " - %s", 
                    activation_str[info->activation_method]);
            strncat(info->vessel_id, temp_str, 
                   sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
        }
        
        // Decode altitude information
        const char* altitude_str[] = {
            "<=400m", ">400m<=800m", ">800m<=1200m", ">1200m<=1600m",
            ">1600m<=2200m", ">2200m<=2800m", ">2800m<=3400m", ">3400m<=4000m",
            ">4000m<=4800m", ">4800m<=5600m", ">5600m<=6600m", ">6600m<=7600m",
            ">7600m<=8800m", ">8800m<=10000m", ">10000m", "N/A"
        };
        
        if (info->auxiliary_device <= 15) {
            snprintf(temp_str, sizeof(temp_str), ", Alt:%s", 
                    altitude_str[info->auxiliary_device]);
            strncat(info->vessel_id, temp_str,
                   sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
        }
        
        // Decode location freshness
        const char* freshness_str[] = {
            "rotating field",
            ">60s old", 
            ">2s<=60s old",
            "<=2s old"
        };
        
        if (info->location_freshness <= 3) {
            snprintf(temp_str, sizeof(temp_str), ", Loc:%s",
                    freshness_str[info->location_freshness]);
            strncat(info->vessel_id, temp_str,
                   sizeof(info->vessel_id) - strlen(info->vessel_id) - 1);
        }
        
        // Assurer la terminaison de la chaîne
        info->vessel_id[sizeof(info->vessel_id) - 1] = '\0';
    }
}

static int validate_frame_sync(const char *frame, int frame_length) {
    (void)frame_length;
    
    // Check bit sync pattern (15 ones)
    for (int i = 0; i < 15; i++) {
        if (frame[i] != '1') {
            printf("Warning: Bit sync pattern error at position %d\n", i);
            return 0;
        }
    }
    
    // Check frame sync pattern
    uint32_t frame_sync = get_bits(frame, 15, 9);
    
    switch (frame_sync) {
        case 0b000101101:  // Normal message
        case 0b001010010:  // Self-test message
        case 0b110101000:  // National use
        case 0b011010000:  // Test protocol (0x0D0)
        case 0b000101111:  // Normal Mode Protocol (0x02F)    
            return 1;
        default:
            printf("Warning: Unknown frame sync pattern: %03X\n", frame_sync);
            return 1;
    }
}

// ===================================================
// Binary to hexadecimal conversion
// ===================================================
static void binary_to_hex(const char *binary, int length, char *hex_output, size_t hex_size) {
    // Clear output buffer
    memset(hex_output, 0, hex_size);
    
    // Process bits 25 to end (skip sync and frame sync patterns)
    int start_bit = 24;  // Start from bit 25 (index 24)
    int hex_index = 0;
    
    // Process 4 bits at a time to create hex digits
    for (int i = start_bit; i < length && hex_index < (int)(hex_size - 1); i += 4) {
        int nibble = 0;
        for (int j = 0; j < 4 && (i + j) < length; j++) {
            nibble = (nibble << 1) | (binary[i + j] == '1' ? 1 : 0);
        }
        hex_output[hex_index++] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
    }
}

// ===================================================
// Main decoding function
// ===================================================
static void decode_1g_frame(const char *frame, int frame_length, BeaconInfo1G *info) {
    memset(info, 0, sizeof(BeaconInfo1G));
    info->frame_type = frame_length;
    info->crc_error = 0;

    // CRC verification
    int crc1_failed = test_crc1(frame);
    int crc2_failed = 0;
    int is_orbitography = 0;

    if (frame_length == LONG_FRAME_BITS) {
        // Check if it's orbitography (protocol 0b000 for user location)
        // Orbitography beacons don't have position data, so CRC2 doesn't apply
        int user_protocol_code = get_bits(frame, 36, 3);
        is_orbitography = (user_protocol_code == 0b000);

        if (!is_orbitography) {
            // Only test CRC2 if NOT orbitography
            crc2_failed = test_crc2(frame);
        }
    }

    // Display CRC status
    if (crc1_failed || crc2_failed) {
        info->crc_error = 1;
        if (is_orbitography) {
            // Orbitography: only show CRC1 (CRC2 N/A)
            printf("CRC: CRC1=%s\n", crc1_failed ? "FAIL" : "OK");
        } else {
            // Normal beacon: show both CRC1 and CRC2
            printf("CRC ERROR: CRC1=%s CRC2=%s\n",
                   crc1_failed ? "FAIL" : "OK",
                   crc2_failed ? "FAIL" : "OK");
        }
    } else {
        // All CRCs OK
        if (is_orbitography) {
            printf("CRC: CRC1=OK\n");
        } else {
            printf("CRC: CRC1=OK CRC2=OK\n");
        }
    }
    
    // Decode country code (bits 27-36, positions 26-35 in 0-indexed)
    info->country_code = get_bits(frame, 26, 10);
    
    // Decode protocol code (bits 37-40, positions 36-39 in 0-indexed)
    int protocol_bits = get_bits(frame, 36, 4);
    info->protocol_bits = protocol_bits;
    
    // Protocol mapping based on T.001
        if (info->frame_type == SHORT_FRAME_BITS) {
        // Short Messages  : User Protocols only (P=1)
        uint8_t protocol_flag = (frame[25] == '1') ? 1 : 0;  // Bit 26
        
        if (protocol_flag != 1) {
            // Short messages should have P=1
            info->protocol = PROTOCOL_UNKNOWN;
        } else {
            // 3 bits Codes  (37-39) for User Protocols
            switch (protocol_bits & 0x7) {  // Take only 3 bits
                case 0: info->protocol = PROTOCOL_USER_PROTOCOL; break;  // Orbitography
                case 1: info->protocol = PROTOCOL_USER_PROTOCOL; break;  // ELT Aviation
                case 2: info->protocol = PROTOCOL_USER_PROTOCOL; break;  // EPIRB Maritime
                case 3: info->protocol = PROTOCOL_USER_PROTOCOL; break;  // Serial User
                case 4: info->protocol = PROTOCOL_USER_PROTOCOL; break;  // National User
                case 5: info->protocol = PROTOCOL_UNKNOWN; break;        // Reserved for 2G
                case 6: info->protocol = PROTOCOL_USER_PROTOCOL; break;  // Radio Call Sign
                case 7: info->protocol = PROTOCOL_TEST; break;           // Test User
                default: info->protocol = PROTOCOL_UNKNOWN;
            }
        }
    } else {
        // Long Messages : Location or User-Location Protocols
        uint8_t protocol_flag = (frame[25] == '1') ? 1 : 0;  // Bit 26
        
        if (protocol_flag == 0) {
            // Location Protocols (P=0, codes 4 bits)
            switch (protocol_bits) {
                case 0: info->protocol = PROTOCOL_UNKNOWN; break;  // Spare
                case 1: info->protocol = PROTOCOL_UNKNOWN; break;  // Spare
                case 2: info->protocol = PROTOCOL_STANDARD_LOCATION; break;  // EPIRB MMSI
                case 3: info->protocol = PROTOCOL_STANDARD_LOCATION; break;  // ELT 24-bit
                case 4: info->protocol = PROTOCOL_STANDARD_LOCATION; break;  // ELT serial
                case 5: info->protocol = PROTOCOL_STANDARD_LOCATION; break;  // ELT operator
                case 6: info->protocol = PROTOCOL_STANDARD_LOCATION; break;  // EPIRB serial
                case 7: info->protocol = PROTOCOL_STANDARD_LOCATION; break;  // PLB serial
                case 8: info->protocol = PROTOCOL_NATIONAL_LOCATION; break;  // National ELT
                case 9: info->protocol = PROTOCOL_EMERGENCY_ELT; break;      // ELT(DT)
                case 10: info->protocol = PROTOCOL_NATIONAL_LOCATION; break; // National EPIRB
                case 11: info->protocol = PROTOCOL_NATIONAL_LOCATION; break; // National PLB
                case 12: info->protocol = PROTOCOL_SHIP_SECURITY; break;     // Ship Security
                case 13: info->protocol = PROTOCOL_RLS_LOCATION; break;      // RLS Location
                case 14: info->protocol = PROTOCOL_TEST; break;              // Standard Test
                case 15: info->protocol = PROTOCOL_TEST; break;              // National Test
                default: info->protocol = PROTOCOL_UNKNOWN;
            }
        } else {
            // User-Location Protocols (P=1, codes 3 bits)
            info->protocol = PROTOCOL_USER_PROTOCOL;
            // The codes are the same than User Protocols but with position
        }
    }
    
      // Decode user identification for User and User-Location protocols
    if (info->protocol == PROTOCOL_USER_PROTOCOL) {
        decode_user_identification(frame, info);
    }
    
    // For ELT-DT, decode ID type first (bits 41-42)
    if (info->protocol == PROTOCOL_EMERGENCY_ELT) {
        info->id_type = get_bits(frame, 40, 2);
        
        // Decode aircraft address if ID type is 0
        if (info->id_type == 0) {
            decode_aircraft_address(frame, info);
        } else {
            strcpy(info->vessel_id, "ID-NOT-AVAIL");
        }
    }
    
    // Decode location based on protocol
switch (info->protocol) {
    case PROTOCOL_STANDARD_LOCATION:
        decode_standard_location(frame, info, frame_length);
        info->base_lat = info->lat;
        info->base_lon = info->lon;
        info->has_position = 1;
        break;
        
    case PROTOCOL_NATIONAL_LOCATION:
        decode_national_location(frame, info, frame_length);
        info->has_position = 1;
        break;
        
    case PROTOCOL_EMERGENCY_ELT:
        decode_elt_dt_location(frame, info);
        info->has_position = 1;
        break;
        
    case PROTOCOL_RLS_LOCATION:
        decode_rls_location(frame, info, frame_length);
        info->has_position = 1;
        break;
        
    case PROTOCOL_USER_PROTOCOL:
        // User-Location protocol - call the decoder
        decode_user_location(frame, info, frame_length);
        // Note: decode_user_location sets has_position appropriately
        // For long frames with User-Location protocol, position IS available
        // For short frames, position is NOT available
        // So we don't override has_position here
        break;
        
    case PROTOCOL_TEST:
        // Test Protocol - Standard Test (protocol 14) or National Test (protocol 15)
        if (info->protocol_bits == 14) {
            decode_standard_test_data(frame, info);
        } else if (info->protocol_bits == 15) {
            decode_national_use_data(frame, info);
            strcpy(info->vessel_id, "National Test");
        } else {
            // User Test Protocol (short frame case 7)
            decode_test_beacon_data(frame, info);
        }
        info->has_position = 0;
        break;
        
    case PROTOCOL_SHIP_SECURITY:
        // Ship Security Protocol (protocol 12) - decode as standard location with security flag
        decode_standard_location(frame, info, frame_length);
        info->base_lat = info->lat;
        info->base_lon = info->lon;
        info->has_position = 1;
        // Override vessel_id to indicate security beacon
        strcat(info->vessel_id, " [SECURITY]");
        break;
        
    default:
        // Unknown protocol - no position data
        info->has_position = 0;
        info->lat = 0.0;
        info->lon = 0.0;
        break;
}

    // Decode supplementary data (adds activation, altitude, freshness info to vessel_id)
    decode_supplementary_data(frame, info);
    
    // Generate protocol string for hex ID
        const char* protocol_str;
    switch (info->protocol) {
        case PROTOCOL_STANDARD_LOCATION: protocol_str = "STD"; break;
        case PROTOCOL_NATIONAL_LOCATION: protocol_str = "NAT"; break;
        case PROTOCOL_USER_PROTOCOL: protocol_str = "USR"; break;
        //case PROTOCOL_USER_PROTOCOL: protocol_str = "ULO"; break;  // User-Location
        case PROTOCOL_TEST: protocol_str = "TST"; break;
        case PROTOCOL_EMERGENCY_ELT: protocol_str = "ELT"; break;
        case PROTOCOL_EMERGENCY_EPIRB: protocol_str = "EPB"; break;
        case PROTOCOL_EMERGENCY_PLB: protocol_str = "PLB"; break;
        case PROTOCOL_RLS_LOCATION: protocol_str = "RLS"; break;
        case PROTOCOL_SHIP_SECURITY: protocol_str = "SEC"; break;
        default: protocol_str = "UNK";
    }
    
    // Extract serial number (different position based on protocol)
    if (info->protocol == PROTOCOL_EMERGENCY_ELT) {
        // For ELT-DT, serial might be in a different location
        info->serial = info->aircraft_address & 0xFFFF;  // Use lower 16 bits of aircraft address
    } else if (info->protocol == PROTOCOL_NATIONAL_LOCATION) {
        // For National Location, serial is already set in decode_national_location
        // It's the 18-bit national ID from bits 41-58
        // No need to extract again, just use what was set
    } else {
        // For other protocols, standard position
        info->serial = get_bits(frame, 50, 14);  // Standard position for serial protocols
    }
    
    // Generate hex ID
    snprintf(info->hex_id, sizeof(info->hex_id), "%s-%s-%04X-%08X",
             (frame_length == LONG_FRAME_BITS) ? "LG" : "SH",
             protocol_str, info->country_code, info->serial);
}
// ===================================================
// Interface function
// ===================================================
void decode_1g(const uint8_t *bits, int length) {
    if (length != SHORT_FRAME_BITS && length != LONG_FRAME_BITS) {
        fprintf(stderr, "ERROR: Invalid frame length: %d bits (expected %d or %d)\n", 
                length, SHORT_FRAME_BITS, LONG_FRAME_BITS);
        return;
    }

    if (!bits) {
        fprintf(stderr, "ERROR: NULL bits array\n");
        return;
    }

    char frame_str[LONG_FRAME_BITS + 1];
    for (int i = 0; i < length; i++) {
        frame_str[i] = (bits[i] & 1) ? '1' : '0';
    }
    frame_str[length] = '\0';
    
    // Generate and display hexadecimal representation
    char hex_str[64];
    binary_to_hex(frame_str, length, hex_str, sizeof(hex_str));
    //printf("Hexadecimal content: %s\n", hex_str);

    if (!validate_frame_sync(frame_str, length)) {
        printf("Warning: Frame synchronization issues detected\n");
    }

    BeaconInfo1G info;
    decode_1g_frame(frame_str, length, &info);
    
    char coord_buf[100] = "Position not available";
    if (info.has_position && (info.lat != 0.0 || info.lon != 0.0)) {
        if (validate_coordinates(info.lat, info.lon)) {
            format_coordinates(info.lat, info.lon, coord_buf, sizeof(coord_buf));
        } else {
            snprintf(coord_buf, sizeof(coord_buf), 
                    "INVALID COORDINATES: %.5f, %.5f", info.lat, info.lon);
        }
    }
    
    if (info.crc_error) {
        printf("\nCRC ERROR - Data may be corrupted\n");
    }
    
    printf("\n=== 406 MHz BEACON DECODE (1G %s) ===", 
           (length == LONG_FRAME_BITS) ? "LONG" : "SHORT");
    
     const char* protocol_name;
    switch (info.protocol) {
        case PROTOCOL_STANDARD_LOCATION: 
            protocol_name = "Standard Location";
            break;
        case PROTOCOL_NATIONAL_LOCATION: 
            protocol_name = "National Location";
            break;
        case PROTOCOL_USER_PROTOCOL:
            protocol_name = "User-Location Protocol";
            break;
        case PROTOCOL_TEST: 
            protocol_name = "Test Protocol";
            break;
        case PROTOCOL_EMERGENCY_ELT: 
            protocol_name = "ELT-DT Location Protocol";
            break;
        case PROTOCOL_EMERGENCY_EPIRB: 
            protocol_name = "Emergency EPIRB";
            break;
        case PROTOCOL_EMERGENCY_PLB: 
            protocol_name = "Emergency PLB";
            break;
        case PROTOCOL_RLS_LOCATION:
            protocol_name = "RLS Location Protocol";
            break;
        case PROTOCOL_SHIP_SECURITY:
            protocol_name = "Ship Security Protocol";
            break;
        default: 
            protocol_name = "Unknown Protocol";
    }
    
    printf("\nProtocol: %d (%s)", info.protocol_bits, protocol_name);
    printf("\nCountry: %u", info.country_code);
    printf("\nHex ID: %s", info.hex_id);
    printf("\nIdentification: %s", info.vessel_id);
    
   // Display coordinates
  // Display base position (PDF-1) if we have position data
  if (info.has_position) {
    printf("\nPosition (PDF-1): %.5f %c, %.5f %c",
           fabs(info.base_lat), (info.base_lat >= 0) ? 'N' : 'S',
           fabs(info.base_lon), (info.base_lon >= 0) ? 'E' : 'W');
    
    // Display offsets for ELT-DT protocol
    if (info.protocol == PROTOCOL_EMERGENCY_ELT && info.location_freshness > 0) {
      printf("\nLocation freshness: %s",
             info.location_freshness == 1 ? "<=2 seconds" :
             info.location_freshness == 2 ? ">2s <=60s" : ">60s");
      printf("\nLatitude offset: %c%d min %d sec",
             (info.lat_offset_sign > 0) ? '+' : '-',
             info.lat_offset_min, info.lat_offset_sec);
      printf("\nLongitude offset: %c%d min %d sec",
             (info.lon_offset_sign > 0) ? '+' : '-',
             info.lon_offset_min, info.lon_offset_sec);
      printf("\nComposite position: %.5f %c, %.5f %c",
             fabs(info.lat), (info.lat >= 0) ? 'N' : 'S',
             fabs(info.lon), (info.lon >= 0) ? 'E' : 'W');
    }
    // Display offsets for National Location protocol (long frame only)
    else if (info.protocol == PROTOCOL_NATIONAL_LOCATION && 
             length == LONG_FRAME_BITS &&
             (info.lat_offset_min != 0 || info.lat_offset_sec != 0 ||
              info.lon_offset_min != 0 || info.lon_offset_sec != 0)) {
      printf("\nPosition source: %s",
             info.position_source ? "Internal GNSS" : "External device");
      printf("\n121.5 MHz Homing: %s",
             info.homing_flag ? "Yes" : "No");
      printf("\nLatitude offset: %c%d min %d sec",
             (info.lat_offset_sign > 0) ? '+' : '-',
             info.lat_offset_min, info.lat_offset_sec);
      printf("\nLongitude offset: %c%d min %d sec",
             (info.lon_offset_sign > 0) ? '+' : '-',
             info.lon_offset_min, info.lon_offset_sec);
      printf("\nComposite position: %.5f %c, %.5f %c",
             fabs(info.lat), (info.lat >= 0) ? 'N' : 'S',
             fabs(info.lon), (info.lon >= 0) ? 'E' : 'W');
    }
  }
/*
  // Open map if we have valid position
  if (info.has_position && validate_coordinates(info.lat, info.lon) &&
      (info.lat != 0.0 || info.lon != 0.0)) {
    printf("\nOpening map location...");
    open_osm_map(info.lat, info.lon);
  } else if (info.has_position) {
    printf("\nMap not opened due to invalid coordinates");
  }
 */ 

    // Open map if we have valid position
    if (info.has_position && validate_coordinates(info.lat, info.lon) &&
        (info.lat != 0.0 || info.lon != 0.0)) {
        // Création du lien OpenStreetMap cliquable
        printf("\nOpenStreetMap: https://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f#map=10/%.6f/%.6f",
               info.lat, info.lon, info.lat, info.lon);
    } else if (info.has_position) {
        printf("\nMap not opened due to invalid coordinates");
    }
/*
// Display clickable map link if position available
    if (info.has_position && validate_coordinates(info.lat, info.lon)) {
        printf("\nCliquez sur le lien pour afficher la carte dans le navigateur:");
        printf("\nhttps://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f#map=6/%.6f/%.6f",
               info.lat, info.lon, info.lat, info.lon);
    }
    
    // Timestamp (keep this as is)
    time_t current_time = time(NULL);
    struct tm *local_time = localtime(&current_time);
    printf("\n[%02d:%02d:%02d] 1G decoding completed\n",
           local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
*/
  printf("\n");
  log_to_terminal("1G decoding completed");
}

// ===================================================
// Additional decoder functions from dec406_V7 
// ===================================================

// Helper function to calculate value from bit range (adapted from dec406_V7)
static int calculate_bit_value(const char *bits, int start, int end) {
    int i, y = 0, x = 1;
    for (i = end; i >= start; i--) {
        if (bits[i] == '1') y = y + x;
        x = 2 * x;
    }
    return y;
}

// Helper function to print hex byte (adapted from dec406_V7)
static void print_hex_byte(int x) {
    int a, b;
    a = x / 16;
    b = x % 16;
    printf("%x", a);
    printf("%x", b);
}

// Orbitography/calibration beacon decoder (adapted from dec406_V7)
static void decode_orbitography_data(const char *bits, BeaconInfo1G *info) {
    int i, j, a;
    
    printf("Orbitography data: ");
    
    // Extract 5 bytes of orbitography data (bits 39-78)
    for (j = 0; j < 5; j++) {
        i = 39 + j * 8;
        a = calculate_bit_value(bits, i, i + 7);
        print_hex_byte(a);
    }
    
    // Extract final 6-bit value (bits 79-84) 
    i = 79;
    a = calculate_bit_value(bits, i, i + 5);
    printf("%02d", a);
    
    // Mark as system beacon with no position data
    info->has_position = 0;
    strcpy(info->hex_id, "SYS-ORBIT");
}

// Baudot character decoder (from dec406_V7)
static char decode_baudot_char(int x) {
    switch(x) {
        case 56: return 'A'; case 51: return 'B'; case 46: return 'C'; case 50: return 'D';
        case 48: return 'E'; case 54: return 'F'; case 43: return 'G'; case 37: return 'H';
        case 44: return 'I'; case 58: return 'J'; case 62: return 'K'; case 41: return 'L';
        case 39: return 'M'; case 38: return 'N'; case 35: return 'O'; case 45: return 'P';
        case 61: return 'Q'; case 42: return 'R'; case 52: return 'S'; case 33: return 'T';
        case 60: return 'U'; case 47: return 'V'; case 57: return 'W'; case 55: return 'X';
        case 53: return 'Y'; case 49: return 'Z'; case 36: return ' '; case 24: return '-';
        case 23: return '/'; case 13: return '0'; case 29: return '1'; case 25: return '2';
        case 16: return '3'; case 10: return '4'; case 1: return '5'; case 21: return '6';
        case 28: return '7'; case 12: return '8'; case 3: return '9';
        default: return '_';
    }
}

// Display 6-character Baudot string (Aviation User Protocol)
static void display_baudot_42(const char *bits) {
    printf(" Call sign: ");
    for (int j = 0; j < 6; j++) {
        int i = 39 + j * 6;
        int a = calculate_bit_value(bits, i, i + 5);
        printf("%c", decode_baudot_char(a));
    }
}

// Display 7-character Baudot string (Aviation Extended)
static void display_baudot_2(const char *bits) {
    printf(" Call sign: ");
    for (int j = 0; j < 7; j++) {
        int i = 39 + j * 6;
        int a = calculate_bit_value(bits, i, i + 5);
        printf("%c", decode_baudot_char(a));
    }
}

// Specific beacon identification (from dec406_V7)
static void display_specific_beacon(const char *bits) {
    printf(" Specific beacon: ");
    int i = 75;
    int a = calculate_bit_value(bits, i, i + 5);
    printf("%c", decode_baudot_char(a));
}

// Standard test protocol decoder (from dec406_V7)
static void decode_standard_test_data(const char *bits, BeaconInfo1G *info) {
    printf("Test data: ");
    
    // Display raw test data (bits 40-63)
    for (int i = 40; i < 64; i++) {
        printf("%c", bits[i]);
    }
    printf(" (hex: ");
    
    // Display as 3 hex bytes
    for (int j = 0; j < 3; j++) {
        int i = 40 + j * 8;
        int a = calculate_bit_value(bits, i, i + 7);
        print_hex_byte(a);
    }
    printf(")");
    
    info->has_position = 0;
    strcpy(info->hex_id, "TEST-STD");
}

// Test beacon data decoder (from dec406_V7)
static void decode_test_beacon_data(const char *bits, BeaconInfo1G *info) {
    printf("Test beacon data: ");
    
    // Extract test data (adapted from dec406_V7)
    for (int j = 0; j < 5; j++) {
        int i = 39 + j * 8;
        int a = calculate_bit_value(bits, i, i + 7);
        print_hex_byte(a);
    }
    
    info->has_position = 0;
    strcpy(info->hex_id, "TEST-USER");
}

// National use decoder (from dec406_V7)
static void decode_national_use_data(const char *bits, BeaconInfo1G *info) {
    printf("National use data: ");
    
    // Extract 5 bytes + 2 additional 6-bit values (from dec406_V7)
    for (int j = 0; j < 5; j++) {
        int i = 39 + j * 8;
        int a = calculate_bit_value(bits, i, i + 7);
        print_hex_byte(a);
    }
    
    // Additional values at bits 79-84 and 106-111
    int a1 = calculate_bit_value(bits, 79, 84);
    int a2 = calculate_bit_value(bits, 106, 111);
    printf("%02d%02d", a1, a2);
    
    strcpy(info->hex_id, "NAT-USE");
}

// Radio Call Sign User Protocol decoder (from dec406_V7)
static void decode_radio_callsign_data(const char *bits, BeaconInfo1G *info) {
    printf("Radio call sign: ");
    
    // Extract radio call sign data using Baudot encoding
    for (int j = 0; j < 7; j++) {
        int i = 39 + j * 6;
        int a = calculate_bit_value(bits, i, i + 5);
        printf("%c", decode_baudot_char(a));
    }
    
    // No position data for call sign protocol
    info->has_position = 0;
    strcpy(info->hex_id, "RADIO-CS");
}
