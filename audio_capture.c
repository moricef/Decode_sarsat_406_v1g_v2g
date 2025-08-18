#include "audio_capture.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

void capture_audio(const char* filename, int duration) {
    char command[256];
    
    // Configuration SoX pour la capture audio
    snprintf(command, sizeof(command),
             "sox -d -b 16 -c 1 -r 48000 %s trim 0 %d silence 1 0.1 1%% 1 0.5 1%%",
             filename, duration);
    
    printf("Début de la capture audio (durée: %d secondes)...\n", duration);
    system(command);
    printf("Capture audio terminée. Fichier sauvegardé: %s\n", filename);
}

int detect_beacon_signal(const char* filename, uint8_t** bits) {
    // Analyse du signal avec SoX et traitement FSK
    char command[512];
    FILE* fp;
    
    snprintf(command, sizeof(command),
             "sox %s -n rate 8k spectrogram -z 90 -w kaiser -o - 2>/dev/null | "
             "grep '406MHz' | wc -l", filename);
    
    fp = popen(command, "r");
    if (!fp) return 0;
    
    int count;
    fscanf(fp, "%d", &count);
    pclose(fp);
    
    return (count > 0);
}
