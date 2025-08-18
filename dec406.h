// dec406.h
#ifndef DEC406_H
#define DEC406_H

#include <stdint.h>

#define FRAME_1G_LENGTH 112
#define FRAME_2G_LENGTH 250

void decode_1g(const uint8_t *bits, int length);
void decode_2g(const uint8_t *bits);
void decode_beacon(const uint8_t *bits, int length);

#endif
