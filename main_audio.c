/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


// main_audio.c - Main program for 406 MHz beacon decoder with audio support
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dec406.h"
#include "audio_capture.h"
#include "display_utils.h"

// ===================================================
// Function prototypes
// ===================================================
void print_help(const char* program_name);
int is_hex_string(const char* str);
int is_wav_file(const char* filename);
void decode_hex_string(const char* hex_str);
void hex_string_to_bits(const char* hex_str, uint8_t* bits, int bit_count);

// ===================================================
// Help function
// ===================================================
void print_help(const char* program_name) {
    printf("\n/*--------- 406 MHz Beacon Decoder ---------*/\n");
    printf("Usage: %s [options] [file.wav | hex_string]\n\n", program_name);
    
    printf("USAGE MODES:\n");
    printf("  1. Real-time audio capture (via sox):\n");
    printf("     sox -t alsa default -t wav - lowpass 3000 highpass 10 gain -l 6 2>/dev/null | %s\n\n", program_name);
    
    printf("  2. WAV file:\n");
    printf("     %s recording.wav\n\n", program_name);
    
    printf("  3. Hexadecimal string:\n");
    printf("     %s FFFED08E39048D158AC01E3AA482856824CE\n\n", program_name);
    
    printf("OPTIONS:\n");
    printf("  --help              Display this help\n");
    printf("  --osm               Open position in OpenStreetMap\n");
    printf("  --une_minute        55-second timeout (for scan406)\n");
    printf("  --verbose           Verbose mode with statistics\n");
    printf("  --canal1            Use right audio channel\n");
    printf("  --2 to --100        Threshold coefficient (default: 100)\n");
    printf("  --M1 to --M10       Max level for threshold (default: M3)\n\n");
    
    printf("SUPPORTED HEX FORMATS:\n");
    printf("  - 28 characters (112 bits): 1G short frame\n");
    printf("  - 36 characters (144 bits): 1G long frame\n");
    printf("  - 64 characters (256 bits): 2G frame\n\n");
    
    printf("INSTALLING SOX (if needed):\n");
    printf("  sudo apt-get install sox\n");
    printf("/*-----------------------------------------------*/\n\n");
}

// ===================================================
// Input type detection
// ===================================================
int is_hex_string(const char* str) {
    size_t len = strlen(str);
    
    // Check if it's a valid hex string
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit(str[i])) {
            return 0;
        }
    }
    
    // Check supported lengths
    return (len == 28 || len == 36 || len == 64);
}

int is_wav_file(const char* filename) {
    size_t len = strlen(filename);
    if (len < 4) return 0;
    
    const char* ext = filename + len - 4;
    return (strcasecmp(ext, ".wav") == 0);
}

// ===================================================
// Hex to bits conversion
// ===================================================
void hex_string_to_bits(const char* hex_str, uint8_t* bits, int bit_count) {
    int hex_len = strlen(hex_str);
    
    for (int i = 0; i < hex_len && (i * 4) < bit_count; i++) {
        char c = tolower(hex_str[i]);
        int value;
        
        if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= '0' && c <= '9') {
            value = c - '0';
        } else {
            value = 0;
            fprintf(stderr, "Warning: non-hex character '%c' at position %d\n", c, i);
        }
        
        // Convert each hex digit to 4 bits
        for (int j = 0; j < 4 && (i * 4 + j) < bit_count; j++) {
            bits[i * 4 + j] = (value >> (3 - j)) & 1;
        }
    }
}

// ===================================================
// Hex string decoding
// ===================================================
void decode_hex_string(const char* hex_str) {
    size_t len = strlen(hex_str);
    int frame_bits;
    
    // Determine frame length in bits
    if (len == 28) {
        frame_bits = 112;  // 1G short
    } else if (len == 36) {
        frame_bits = 144;  // 1G long
    } else if (len == 64) {
        frame_bits = 250;  // 2G (uses 250 of 256 bits)
    } else {
        fprintf(stderr, "Unsupported hex string length: %zu characters\n", len);
        return;
    }
    
    // Allocate bit array
    uint8_t* bits = malloc(frame_bits * sizeof(uint8_t));
    if (!bits) {
        fprintf(stderr, "Memory allocation error\n");
        return;
    }
    
    // Convert hex string to bits
    memset(bits, 0, frame_bits * sizeof(uint8_t));
    hex_string_to_bits(hex_str, bits, frame_bits);
    
    // Call decoder with bits
    decode_beacon(bits, frame_bits);
    
    free(bits);
}

// ===================================================
// Main function
// ===================================================
int main(int argc, char *argv[]) {
    FILE *input_file = NULL;
    int is_hex_input = 0;
    int is_wav_input = 0;
    char *input_arg = NULL;
    int show_help = 0;
    
    // Initialize audio capture
    init_audio_capture();
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help = 1;
        }
        else if (argv[i][0] == '-') {
            // Option, will be processed by process_audio_options
            continue;
        }
        else if (input_arg == NULL) {
            input_arg = argv[i];
        }
    }
    
    // Show help if requested or no arguments
    if (show_help || (argc == 1)) {
        print_help(argv[0]);
        return 0;
    }
    
    // Process audio options
    process_audio_options(argc, argv);
    
    // Determine input type
    if (input_arg != NULL) {
        if (is_hex_string(input_arg)) {
            is_hex_input = 1;
            printf("Decoding hexadecimal string...\n");
        } else if (is_wav_file(input_arg)) {
            is_wav_input = 1;
            printf("Processing WAV file: %s\n", input_arg);
        } else {
            fprintf(stderr, "Error: Unknown input format\n");
            fprintf(stderr, "Input must be a WAV file or valid hex string\n");
            return 1;
        }
    }
    
    // Process based on input type
    if (is_hex_input) {
        // Decode hex string directly
        decode_hex_string(input_arg);
    }
    else if (is_wav_input) {
        // Open WAV file
        input_file = fopen(input_arg, "rb");
        if (!input_file) {
            fprintf(stderr, "Error: Cannot open file %s\n", input_arg);
            return 1;
        }
        
        // Read WAV header
        if (lit_entete_wav(input_file) != 0) {
            fprintf(stderr, "Error: Invalid WAV file format\n");
            fclose(input_file);
            return 1;
        }
        
        // Capture and decode frames
        printf("Starting frame capture...\n");
        int frames_decoded = 0;
        while (capture_trame(input_file)) {
            frames_decoded++;
            printf("\n--- Frame %d decoded ---\n", frames_decoded);
        }
        
        if (frames_decoded == 0) {
            printf("No frames detected in file\n");
        } else {
            printf("\nTotal frames decoded: %d\n", frames_decoded);
        }
        
        fclose(input_file);
    }
    else {
        // Read from stdin (pipe from sox)
        printf("Reading from stdin (waiting for audio data)...\n");
        printf("Press Ctrl+C to stop\n\n");
        
        // Check if stdin is a WAV stream
        if (lit_entete_wav(stdin) != 0) {
            // Not a WAV file, try raw samples
            printf("No WAV header detected, assuming raw audio\n");
            // Set default parameters for raw audio
            f_ech = 48000;
            bits = 16;
            N_canaux = 1;
            ech_par_bit = f_ech / bauds;
        }
        
        // Continuous capture loop
        int frames_decoded = 0;
        while (1) {
            if (capture_trame(stdin)) {
                frames_decoded++;
                printf("\n--- Frame %d decoded ---\n", frames_decoded);
            }
            
            // Check for EOF
            if (feof(stdin)) {
                break;
            }
        }
        
        if (frames_decoded > 0) {
            printf("\nTotal frames decoded: %d\n", frames_decoded);
        }
    }
    
    return 0;
}
