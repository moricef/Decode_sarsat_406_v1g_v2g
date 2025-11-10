#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dsss_demod.h"

int main() {
    int8_t *prn_i = malloc(38400);
    int8_t *prn_q = malloc(38400);
    dsss_generate_prn_sequences(prn_i, prn_q);
    
    printf("Chip [0]: I=%+d, Q=%+d\n", prn_i[0], prn_q[0]);
    printf("Chip [1]: I=%+d, Q=%+d\n", prn_i[1], prn_q[1]);
    printf("Chip [2]: I=%+d, Q=%+d\n", prn_i[2], prn_q[2]);
    
    return 0;
}
