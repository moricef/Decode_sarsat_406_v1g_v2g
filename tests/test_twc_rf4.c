#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dec406.h"

#define BCH_GEN 0x1C7EB85DF3C97ULL

static void put_bits(uint8_t *bits, int start, int count, uint32_t value)
{
    for (int i = 0; i < count; i++)
        bits[start + i] = (value >> (count - 1 - i)) & 1;
}

static void encode_bch(uint8_t cw[250])
{
    uint8_t work[250];
    memcpy(work, cw, 202);
    memset(work + 202, 0, 48);

    for (int i = 0; i < 202; i++) {
        if (!work[i]) continue;
        for (int j = 0; j <= 48; j++)
            work[i + j] ^= (BCH_GEN >> (48 - j)) & 1;
    }
    memcpy(cw + 202, work + 202, 48);
}

static void base_frame(uint8_t cw[250])
{
    memset(cw, 0, 250);
    put_bits(cw, 0, 16, 65535);       /* system-beacon TAC */
    put_bits(cw, 16, 14, 8615);
    put_bits(cw, 30, 10, 227);        /* France */
    cw[42] = 1;                       /* test protocol */
    put_bits(cw, 90, 3, 7);           /* reserved system ID */
    put_bits(cw, 137, 3, 7);          /* system beacon */
    memset(cw + 140, 1, 14);          /* main-field spare bits */

    put_bits(cw, 154, 4, 4);          /* RF#4 */
    put_bits(cw, 158, 3, 1);          /* Galileo */
    put_bits(cw, 161, 5, 3);          /* dataset version */
    cw[166] = 1;                      /* Type-3 ACK received */
}

static void run_short(void)
{
    uint8_t cw[250];
    base_frame(cw);
    cw[167] = 0;                      /* short format */
    put_bits(cw, 169, 7, 15);
    put_bits(cw, 176, 4, 2);
    put_bits(cw, 180, 7, 22);
    put_bits(cw, 187, 4, 1);
    put_bits(cw, 191, 7, 8);
    put_bits(cw, 198, 4, 4);
    encode_bch(cw);

    puts("\n######## RF#4 SHORT: expect 15/2, 22/1, 8/4 ########");
    decode_2g(cw);
}

static void run_long(void)
{
    uint8_t cw[250];
    base_frame(cw);
    cw[167] = 1;                      /* long format */
    put_bits(cw, 169, 7, 42);
    put_bits(cw, 176, 15, 0x4215);
    put_bits(cw, 191, 7, 9);
    put_bits(cw, 198, 4, 3);
    encode_bch(cw);

    puts("\n######## RF#4 LONG: expect 42/0x4215, 9/3 ########");
    decode_2g(cw);
}

static void run_cancellation(uint8_t method, const char *expected)
{
    uint8_t cw[250];
    base_frame(cw);
    put_bits(cw, 154, 4, 15);
    for (int i = 158; i < 200; i++) cw[i] = 1;
    put_bits(cw, 200, 2, method);
    encode_bch(cw);

    printf("\n######## RF#15 METHOD %u: expect %s ########\n", method, expected);
    decode_2g(cw);
}

int main(void)
{
    run_short();
    run_long();
    run_cancellation(1, "automatic external");
    run_cancellation(2, "manual user");
    return 0;
}
