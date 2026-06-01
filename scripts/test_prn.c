/* test_prn: dump PRN_I (38400) then PRN_Q (38400) as raw bytes (0/1). */
#include "despread.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int8_t *prn_i = malloc(DESPREAD_PRN_LEN);
    int8_t *prn_q = malloc(DESPREAD_PRN_LEN);
    despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PRN_LEN, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, DESPREAD_PRN_LEN, prn_q);
    fwrite(prn_i, 1, DESPREAD_PRN_LEN, stdout);
    fwrite(prn_q, 1, DESPREAD_PRN_LEN, stdout);
    fprintf(stderr, "Wrote %d + %d PRN chips\n", DESPREAD_PRN_LEN, DESPREAD_PRN_LEN);
    free(prn_i); free(prn_q);
    return 0;
}
