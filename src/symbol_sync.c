/**
 * @file symbol_sync.c
 * @brief Symbol timing — Stage A (fixed-phase decimation by max energy)
 */

#include "symbol_sync.h"
#include <float.h>

int symbol_sync_decimate(const float complex *input, size_t input_length,
                         int sps, int preamble_chips,
                         float complex *output, size_t *output_length)
{
    if (input == NULL || output == NULL || sps < 1 || preamble_chips < 1) {
        if (output_length) *output_length = 0;
        return -1;
    }

    /* Limit phase-search examination to what fits in the buffer. */
    size_t max_chips = input_length / (size_t)sps;
    int n_chips = (preamble_chips < (int)max_chips) ? preamble_chips : (int)max_chips;
    if (n_chips < 1) {
        if (output_length) *output_length = 0;
        return -1;
    }

    /* Phase search: pick phi maximizing energy over n_chips. */
    int best_phase = 0;
    float best_energy = -FLT_MAX;
    for (int phi = 0; phi < sps; phi++) {
        float energy = 0.0f;
        for (int k = 0; k < n_chips; k++) {
            size_t idx = (size_t)phi + (size_t)k * (size_t)sps;
            if (idx >= input_length) break;
            float re = __real__ input[idx];
            float im = __imag__ input[idx];
            energy += re * re + im * im;
        }
        if (energy > best_energy) {
            best_energy = energy;
            best_phase = phi;
        }
    }

    /* Decimate at best_phase, stride sps. */
    size_t out = 0;
    for (size_t i = (size_t)best_phase; i < input_length; i += (size_t)sps)
        output[out++] = input[i];

    if (output_length) *output_length = out;
    return best_phase;
}
