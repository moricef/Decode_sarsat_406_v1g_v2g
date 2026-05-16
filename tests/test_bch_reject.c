/* Exercises the BCH-reject path of decode_2g(): a codeword with more
 * errors than BCH(250,202) can correct must abort the decode and print
 * no beacon fields. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dec406.h"

static void run(const char *label, uint8_t *cw) {
    printf("\n########## %s ##########\n", label);
    decode_2g(cw);
}

int main(void) {
    uint8_t cw[250];

    memset(cw, 0, sizeof(cw));
    run("CASE 1: clean codeword (expect beacon printed)", cw);

    memset(cw, 0, sizeof(cw));
    cw[10] ^= 1; cw[80] ^= 1; cw[200] ^= 1;
    run("CASE 2: 3 errors (expect 'errors corrected', beacon printed)", cw);

    memset(cw, 0, sizeof(cw));
    for (int i = 0; i < 250; i += 8) cw[i] ^= 1;   /* ~32 errors */
    run("CASE 3: 32 errors (expect FRAME REJECTED, no beacon)", cw);

    return 0;
}
