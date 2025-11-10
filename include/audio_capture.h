/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/

// audio_capture.h - Header for audio capture module
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdio.h>

// ===================================================
// Constants
// ===================================================
#define SEUIL 2000
#define MAX_FRAME_BITS 200

// ===================================================
// Global variables (exposed for configuration)
// ===================================================
extern int bauds;           // Baud rate (400 by default)
extern int f_ech;          // Sample rate
extern int ech_par_bit;    // Samples per bit
extern int longueur_trame; // Frame length
extern int bits;           // Bits per sample (8 or 16)
extern int N_canaux;       // Number of channels
extern int opt_minute;     // 55-second timeout option
extern int canal_audio;    // Audio channel to use (0 or 1)
extern int n_ech;          // Sample counter
extern char s[200];        // Decoded bit string
extern double Y[242];      // Correlation values
extern double coeff;       // Threshold coefficient

// ===================================================
// Main functions
// ===================================================

/**
 * Read WAV file header and extract audio parameters
 * @param fp File pointer to WAV file
 * @return 0 on success, 1 on error
 */
int lit_entete_wav(FILE *fp);

/**
 * Read one audio sample from file
 * @param fp File pointer to audio stream
 * @return Sample value or 1000000 on EOF
 */
int lit_ech(FILE *fp);

/**
 * Capture and decode a complete frame
 * @param fp File pointer to audio stream
 * @return 1 if frame decoded successfully, 0 otherwise
 */
int capture_trame(FILE *fp);

/**
 * Process command line options for audio capture
 * @param argc Argument count
 * @param argv Argument vector
 */
void process_audio_options(int argc, char *argv[]);

/**
 * Initialize audio capture parameters
 */
void init_audio_capture(void);

#endif // AUDIO_CAPTURE_H
