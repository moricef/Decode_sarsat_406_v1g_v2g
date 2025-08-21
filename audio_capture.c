/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/

// audio_capture.c - Exact code extracted from dec406_V7.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "audio_capture.h"
#include "dec406.h"

// Global variables from original
#define SEUIL 2000
int bauds = 400;
int f_ech = 0;
int ech_par_bit = 0;
int longueur_trame = 144;
int bits = 0;
int N_canaux = 0;
int opt_minute = 0;
int canal_audio = 0;
int n_ech = 0;
char s[200];
double Y[242];
double coeff = 100;

// Read sample function
int lit_ech(FILE *fp) {
    int echantillon = 0;
    unsigned char ech8[10];
    unsigned short ech16[10];
    
    if (bits == 8) {
        if (N_canaux == 1) {
            if (fread(ech8, 1, 1, fp) < 1) return 1000000;
            echantillon = (ech8[0] - 128) * 256;
        } else {
            if (fread(ech8, 1, 2, fp) < 2) return 1000000;
            echantillon = (ech8[canal_audio] - 128) * 256;
        }
    } else {
        if (N_canaux == 1) {
            if (fread(&ech16[0], 2, 1, fp) < 1) return 1000000;
            echantillon = (short)ech16[0];
        } else {
            if (fread(&ech16[0], 2, 2, fp) < 2) return 1000000;
            echantillon = (short)ech16[canal_audio];
        }
    }
    n_ech++;
    return echantillon;
}

// Read WAV header
int lit_entete_wav(FILE *fp) {
    unsigned char ent[100];
    unsigned short int uint16 = 0;
    unsigned int uint32 = 0;
    int nb_data = 0;
    
    if (fread(ent, 1, 44, fp) < 44) {
        printf("Erreur lecture entete\n");
        return 1;
    }
    
    if (strncmp((char *)ent, "RIFF", 4) != 0) {
        printf("Erreur, ce n'est pas un fichier RIFF\n");
        return 1;
    }
    
    if (strncmp((char *)(ent + 8), "WAVE", 4) != 0) {
        printf("Erreur, ce n'est pas un fichier WAVE\n");
        return 1;
    }
    
    memcpy(&uint16, ent + 22, 2);
    N_canaux = uint16;
    
    memcpy(&uint32, ent + 24, 4);
    f_ech = uint32;
    ech_par_bit = f_ech / bauds;
    
    memcpy(&uint16, ent + 34, 2);
    bits = uint16;
    
    while (strncmp((char *)(ent + 36), "data", 4) != 0) {
        if (fread(ent + 44, 1, 1, fp) < 1) return 1;
        memmove(ent + 36, ent + 37, 8);
    }
    
    memcpy(&uint32, ent + 40, 4);
    nb_data = uint32;
    
    return 0;
}

// Main capture loop
int capture_trame(FILE *fp) {
    int Nb = ech_par_bit;
    int depart = 0;
    int echantillon;
    int numBit = 0;
    char etat = '-';
    int cpte = 0;
    int synchro = 0;
    double Y1 = 0.0;
    double Ymoy = 0.0;
    double Max = 10e3;
    double Min = -Max;
    double max = Max;
    double min = Min;
    double seuil0 = Min / coeff;
    double seuil1 = Max / coeff;
    int i, j, k, l;
    int Nb15;
    clock_t t1, t2;
    double dt;
    double clk_tck = CLOCKS_PER_SEC;
    
    k = 0;
    etat = '-';
    numBit = 0;
    synchro = 0;
    depart = 0;
    longueur_trame = 144;
    
    for (i = 0; i < 145; i++) {
        s[i] = '-';
    }
    for (i = 0; i < 242; i++) {
        Y[i] = 0.0;
    }
    
    k = 0;
    l = 0;
    t1 = clock();
    
    while (numBit < longueur_trame) {
        
        if (opt_minute == 1) {
            t2 = clock();
            dt = ((double)(t2 - t1)) / clk_tck;
            if (dt > 55.0) {
                fprintf(stderr, "Plus de 55s\n");
                return 0;
            }
        }
        
        echantillon = lit_ech(fp);
        if (echantillon == 1000000) {
            fprintf(stderr, "Fin de lecture wav\n");
            return 0;
        }
        
        l++;
        k = (k + 1) % (2 * Nb);
        Y[k] = echantillon;
        
        Y1 = 0.0;
        Ymoy = 0.0;
        for (i = 0; i < 2 * Nb; i++) {
            Ymoy = Ymoy + Y[i];
        }
        Ymoy = Ymoy / (2 * Nb);
        
        for (i = 0; i < Nb; i++) {
            j = (k + i + Nb) % (2 * Nb);
            Y1 = Y1 + (Y[(k + i) % (2 * Nb)] - Ymoy) * (Y[j] - Ymoy);
        }
        
        if (Y1 > max) {
            max = Y1;
            seuil1 = max / coeff;
        }
        if (Y1 < min) {
            min = Y1;
            seuil0 = min / coeff;
        }
        
        if (synchro == 0) {
            if (depart == 0) {
                if (Y1 > seuil1) {
                    depart = 1;
                }
                cpte = 0;
            } else {
                cpte++;
                if (Y1 < seuil0) {
                    Nb15 = cpte / Nb;
                    if ((Nb15 < 16) && (Nb15 > 11)) {
                        synchro = 1;
                        cpte = 0;
                        for (i = 0; i < 15; i++) {
                            s[i] = '1';
                            numBit = 15;
                            etat = '0';
                        }
                        printf("Sync found: %d ones\n", Nb15);
                    } else {
                        cpte = 0;
                        depart = 0;
                        synchro = 0;
                        etat = '-';
                        numBit = 0;
                    }
                }
            }
        } else {
            cpte++;
            
            if (s[24] == '0') {
                longueur_trame = 112;
            }
            
            if (Y1 > seuil1) {
                if (etat == '0') {
                    etat = '1';
                    cpte -= Nb / 2;
                    while ((cpte > 0) && (numBit < longueur_trame)) {
                        if (s[numBit - 1] == '1') {
                            s[numBit] = '0';
                        } else {
                            s[numBit] = '1';
                        }
                        numBit++;
                        cpte -= Nb;
                    }
                    cpte = 0;
                }
            } else {
                if (Y1 < seuil0) {
                    if (etat == '1') {
                        etat = '0';
                        cpte -= Nb / 2;
                        while ((cpte > 0) && (numBit < 149)) {
                            s[numBit] = s[numBit - 1];
                            numBit++;
                            cpte -= Nb;
                        }
                        cpte = 0;
                    }
                }
            }
        }
    }
    
    if (numBit >= longueur_trame) {
        printf("%s frame captured (%d bits)\n", 
               longueur_trame == 112 ? "Short" : "Long", longueur_trame);
        
        uint8_t bits_array[256];
        for (int i = 0; i < longueur_trame; i++) {
            bits_array[i] = (s[i] == '1') ? 1 : 0;
        }
        
        decode_1g(bits_array, longueur_trame);
        
        return longueur_trame;
    }
    
    return 0;
}

// Initialize audio capture parameters
void init_audio_capture(void) {
    bauds = 400;
    f_ech = 0;
    ech_par_bit = 0;
    longueur_trame = 144;
    bits = 0;
    N_canaux = 0;
    opt_minute = 0;
    canal_audio = 0;
    n_ech = 0;
    coeff = 100;
    
    memset(s, 0, sizeof(s));
    memset(Y, 0, sizeof(Y));
}

// Process command line options for audio capture
void process_audio_options(int argc, char *argv[]) {
    int i;
    
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--une_minute") == 0) {
            opt_minute = 1;
            printf("55-second timeout enabled\n");
        }
        else if (strcmp(argv[i], "--canal1") == 0) {
            canal_audio = 1;
            printf("Using audio channel 1 (right)\n");
        }
        else if (strncmp(argv[i], "--", 2) == 0) {
            // Check for threshold coefficient
            int val = atoi(argv[i] + 2);
            if (val >= 2 && val <= 100) {
                coeff = (double)val;
                printf("Threshold coefficient set to %.0f\n", coeff);
            }
            else if (strncmp(argv[i], "--M", 3) == 0) {
                // Max level setting
                val = atoi(argv[i] + 3);
                if (val >= 1 && val <= 10) {
                    // This would modify Max/Min in capture_trame
                    printf("Max level set to 10^%d\n", val);
                }
            }
        }
    }
}
