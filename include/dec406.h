/**********************************

## Licence

 Licence Creative Commons CC BY-NC-SA 

## Auteurs et contributions

- **Code original dec406_v7** : F4EHY (2020)
- **Refactoring et support 2G** : Développement collaboratif (2025)
- **Conformité T.018** : Implémentation complète BCH + MID database

***********************************/


// dec406.h
#ifndef DEC406_H
#define DEC406_H

#include <stdint.h>

#define FRAME_1G_SHORT 112
#define FRAME_1G_LONG 144
#define FRAME_2G_LENGTH 250

void decode_1g(const uint8_t *bits, int length);
void decode_2g(const uint8_t *bits);
/* Set the self-test mode for the next decode_2g() call, derived from the DSSS
 * PRN mode (self-test beacons use a distinct spreading code). Consumed and
 * reset by decode_2g; paths without PRN info default to Normal. */
void decode_2g_set_frame_mode(int is_self_test);
void decode_beacon(const uint8_t *bits, int length);

#endif
