#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdint.h>

void capture_audio(const char* filename, int duration);
int detect_beacon_signal(const char* filename, uint8_t** bits);

#endif
