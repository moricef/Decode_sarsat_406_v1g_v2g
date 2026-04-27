/* test_rrc: compute 11*64 RRC taps and dump as float32 LE to stdout. */
#include "rrc_filter.h"
#include <stdio.h>

int main(void) {
    float taps[1024];
    int n = rrc_compute_taps(1.0f, 2457600.0f, 38400.0f, 0.5f, 11 * 64, taps);
    if (n <= 0) return 1;
    fwrite(taps, sizeof(float), (size_t)n, stdout);
    fprintf(stderr, "Wrote %d taps\n", n);
    return 0;
}
