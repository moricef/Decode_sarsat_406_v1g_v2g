/* fgb_iq_demod.h — IQ-direct FGB (1G) demodulator/decoder.
 *
 * Replaces the legacy FM-demod + F4EHY-audio-slicer pipeline.
 * Works directly on complex baseband IQ samples — no audio detour.
 */

#ifndef FGB_IQ_DEMOD_H
#define FGB_IQ_DEMOD_H

#include <complex.h>
#include <stddef.h>
#include <stdint.h>

#define FGB_LONG_BITS  144
#define FGB_SHORT_BITS 112

/* Decode an FGB burst from complex baseband IQ samples.
 *
 * Input:
 *   iq          — complex baseband samples (mixed to ~0 Hz by the caller).
 *   n           — number of complex samples.
 *   samp_rate   — sample rate in Hz.
 *   burst_start — sample index in iq[] where the burst RF onset is expected
 *                 (caller knows this from its detector + pre-roll geometry).
 *                 Detector jitter ±few ms is tolerated by the CW-anchored
 *                 freq/phase acquisition.
 *   out_bits    — output 144-bit array (caller-allocated).
 *   out_length  — receives 112 for a short frame or 144 for a long frame.
 *
 * Returns:
 *   0   on success (bits valid, CRC OK).
 *   -1  on failure (buffer too short / acquisition out of bounds).
 *   -2  on CRC failure (bits sliced but neither polarity validates).
 *
 * The caller may inspect out_bits on -2 (best-effort decode attempt).
 */
int fgb_iq_decode(const float complex *iq, size_t n, int samp_rate,
                  long burst_start, uint8_t out_bits[FGB_LONG_BITS],
                  int *out_length);

#endif /* FGB_IQ_DEMOD_H */
