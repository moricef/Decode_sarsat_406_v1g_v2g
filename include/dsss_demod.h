/**
 * @file dsss_demod.h
 * @brief DSSS OQPSK Receiver API — pure C implementation
 *
 * Replaces the previous MATLAB Coder wrapper. Implements the receive chain
 * validated bit-perfect (248/248 bits) against the SARSAT 2G modulator:
 *
 *   delay Q by SPS/2  →  RRC matched filter  →  Stage A symbol sync (energy
 *   max + decimation)  →  Costas loop (QPSK, BW=0.0628)  →  binary slicer  →
 *   T.018 despreader (PRN sync 2-pass + per-bit majority over 256 chips).
 *
 * The output of dsss_receive_burst() is 250 bits suitable for direct
 * consumption by decode_2g(rx_bits[250]) in dec406_v2g.c (BCH(250,202) +
 * frame parsing).
 */

#ifndef DSSS_DEMOD_H
#define DSSS_DEMOD_H

#include <stdint.h>
#include <stddef.h>
#include <complex.h>
#include "despread.h"

/* T.018 SGB receive parameters (must match the modulator). */
#define DSSS_CHIP_RATE          38400      /* 38.4 kchips/s */
#define DSSS_SPREADING_FACTOR   256        /* chips per data bit */
#define DSSS_SAMP_RATE_HZ       2457600    /* 2.4576 MHz (SPS=64) */
#define DSSS_SPS                64

/* Frame structure (per T.018 §2.2.3). */
#define DSSS_PREAMBLE_BITS      50         /* total preamble (25 I + 25 Q) */
#define DSSS_PAYLOAD_BITS       202        /* information bits */
#define DSSS_PARITY_BITS        48         /* BCH parity */
#define DSSS_PACKET_BITS        300        /* 50 + 202 + 48 */

/**
 * @brief Receive and demodulate one DSSS OQPSK burst from an IQ buffer.
 *
 * @param ota_buffer    Complex baseband samples (cf32).
 * @param buffer_length Sample count (≥ fs samples for one full burst).
 * @param sps           Samples per chip; may be fractional (e.g. 46.875).
 * @param fs            Sampling frequency in Hz.
 * @param max_doppler   Reserved (Doppler compensation deferred to Stage B).
 * @param output_bits   Output buffer of 250 bytes (each is a bit, 0 or 1)
 *                      ready to feed decode_2g().
 * @param z_score       If non-NULL, receives combined z-score from despread sync.
 *
 * @return 0 on success; -1 on parameter error or sync failure.
 */
int dsss_receive_burst(const float complex *ota_buffer,
                       size_t buffer_length,
                       float sps,
                       float fs,
                       int max_doppler,
                       uint8_t *output_bits,
                       float *z_score);

int dsss_receive_burst_ex(const float complex *ota_buffer,
                          size_t buffer_length,
                          float sps,
                          float fs,
                          int max_doppler,
                          uint8_t *output_bits,
                          float *z_score,
                          despread_prn_mode_t *prn_mode);

#endif /* DSSS_DEMOD_H */
