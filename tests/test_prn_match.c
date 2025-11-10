#include <stdio.h>
#include <stdint.h>

#define PRN_INIT_NORMAL_I   0x000001
#define PRN_INIT_NORMAL_Q   0x1AC1FC

void generate_prn(uint32_t initial_state, int8_t *output, size_t num_chips) {
    uint32_t lfsr = initial_state & 0x7FFFFF;

    for (size_t i = 0; i < num_chips; i++) {
        output[i] = (lfsr & 1) ? -1 : +1;
        uint32_t feedback = (lfsr ^ (lfsr >> 18)) & 1;
        lfsr = (lfsr >> 1) | (feedback << 22);
        lfsr &= 0x7FFFFF;
    }
}

int main() {
    int8_t prn_i[64];
    int8_t prn_q[64];

    generate_prn(PRN_INIT_NORMAL_I, prn_i, 64);
    generate_prn(PRN_INIT_NORMAL_Q, prn_q, 64);

    printf("First 64 chips Normal I (expected: 8000 0108 4212 84A1):\n");
    for (int i = 0; i < 64; i += 4) {
        uint8_t nibble = 0;
        for (int b = 0; b < 4; b++) {
            int chip_val = (prn_i[i+b] == -1) ? 1 : 0;
            nibble = (nibble << 1) | chip_val;
        }
        printf("%X", nibble);
        if ((i + 4) % 16 == 0) printf(" ");
    }
    printf("\n\n");

    printf("First 64 chips Normal Q (expected: 3F83 58BA D030 F231):\n");
    for (int i = 0; i < 64; i += 4) {
        uint8_t nibble = 0;
        for (int b = 0; b < 4; b++) {
            int chip_val = (prn_q[i+b] == -1) ? 1 : 0;
            nibble = (nibble << 1) | chip_val;
        }
        printf("%X", nibble);
        if ((i + 4) % 16 == 0) printf(" ");
    }
    printf("\n\n");

    printf("First 10 chips I: ");
    for (int i = 0; i < 10; i++) printf("%+d ", prn_i[i]);
    printf("\n");

    printf("First 10 chips Q: ");
    for (int i = 0; i < 10; i++) printf("%+d ", prn_q[i]);
    printf("\n");

    return 0;
}
