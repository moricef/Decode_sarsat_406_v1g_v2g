/**
 * @file despread.c
 * @brief T.018 SGB DSSS despreader (PRN gen + 2-pass sync + despread)
 *
 * Direct port of decode_sgb_epy_block_0.py (validated bit-perfect 248/248
 * on the modulator output). Same LFSR seeds, same sync logic, same per-bit
 * majority despread with the four Costas-phase formulas.
 *
 * Input: chip-rate complex samples (output of the Costas loop). Internal
 *        slicer: chip_I = (Re>=0) ? 1 : 0, chip_Q = (Im>=0) ? 1 : 0.
 * Output: 250 message bits interleaved I[0],Q[0],...,I[124],Q[124].
 */

#include "despread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void despread_gen_prn(uint32_t seed, int length, int8_t *out)
{
    uint32_t state = seed;
    for (int i = 0; i < length; i++) {
        out[i] = (int8_t)(state & 1u);
        uint32_t fb = (state ^ (state >> 18)) & 1u;
        state = (state >> 1) | (fb << 22);
    }
}

/* Count chip matches between two int8 sequences over `n` chips.
 * Both sequences are in {0, 1}. */
static int count_matches(const int8_t *a, const int8_t *b, int n)
{
    int m = 0;
    for (int i = 0; i < n; i++)
        if (a[i] == b[i]) m++;
    return m;
}

/* Build a "NOT" copy of a chip sequence: 1 - x. */
static void chip_not(const int8_t *src, int n, int8_t *dst)
{
    for (int i = 0; i < n; i++) dst[i] = (int8_t)(1 - src[i]);
}

int despread_sync(const float complex *samples, int num_chips,
                  despread_sync_t *sync)
{
    if (samples == NULL || sync == NULL ||
        num_chips < DESPREAD_PREAMBLE_CHIPS + DESPREAD_SYNC_RANGE)
        return -1;

    memset(sync, 0, sizeof(*sync));

    /* Slice preamble region. */
    int slice_len = num_chips;
    if (slice_len > DESPREAD_PREAMBLE_CHIPS + DESPREAD_SYNC_RANGE)
        slice_len = DESPREAD_PREAMBLE_CHIPS + DESPREAD_SYNC_RANGE;

    int8_t *chip_i = (int8_t *)malloc((size_t)slice_len);
    int8_t *chip_q = (int8_t *)malloc((size_t)slice_len);
    if (!chip_i || !chip_q) { free(chip_i); free(chip_q); return -1; }

    for (int k = 0; k < slice_len; k++) {
        chip_i[k] = (__real__ samples[k] >= 0.0f) ? 1 : 0;
        chip_q[k] = (__imag__ samples[k] >= 0.0f) ? 1 : 0;
    }

    /* PRN + predictions (preamble length only). */
    int8_t *prn_i = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    int8_t *prn_q = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    int8_t *npi   = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    int8_t *npq   = (int8_t *)malloc(DESPREAD_PREAMBLE_CHIPS);
    if (!prn_i || !prn_q || !npi || !npq) {
        free(chip_i); free(chip_q);
        free(prn_i); free(prn_q); free(npi); free(npq);
        return -1;
    }
    despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PREAMBLE_CHIPS, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, DESPREAD_PREAMBLE_CHIPS, prn_q);
    chip_not(prn_i, DESPREAD_PREAMBLE_CHIPS, npi);
    chip_not(prn_q, DESPREAD_PREAMBLE_CHIPS, npq);

    const int8_t *pred_i_tab[4] = { npi, prn_q, prn_i, npq };
    const int8_t *pred_q_tab[4] = { npq, npi,   prn_q, prn_i };

    /* Pass A — I channel. */
    int search_hi = DESPREAD_SYNC_RANGE;
    if (DESPREAD_PREAMBLE_CHIPS + search_hi > slice_len)
        search_hi = slice_len - DESPREAD_PREAMBLE_CHIPS;

    int best_i = -1, best_off_i = 0, best_phase = 0;
    for (int off = 0; off < search_hi; off++) {
        for (int p = 0; p < 4; p++) {
            int s = count_matches(chip_i + off, pred_i_tab[p],
                                  DESPREAD_PREAMBLE_CHIPS);
            if (s > best_i) { best_i = s; best_off_i = off; best_phase = p; }
        }
    }

    /* Pass B — Q channel. */
    int best_q = -1, best_off_q = best_off_i;
    int qlo = best_off_i - 5; if (qlo < 0) qlo = 0;
    int qhi = best_off_i + 6; if (qhi > search_hi) qhi = search_hi;
    for (int off = qlo; off < qhi; off++) {
        int s = count_matches(chip_q + off, pred_q_tab[best_phase],
                              DESPREAD_PREAMBLE_CHIPS);
        if (s > best_q) { best_q = s; best_off_q = off; }
    }

    int thr = (int)(DESPREAD_SYNC_THRESHOLD * (float)DESPREAD_PREAMBLE_CHIPS);
    if (best_i < thr || best_q < thr) {
        fprintf(stderr,
                "[despread] SYNC FAILED: I=%.1f%% Q=%.1f%% (need %.0f%% each)\n",
                100.0f * best_i / DESPREAD_PREAMBLE_CHIPS,
                100.0f * best_q / DESPREAD_PREAMBLE_CHIPS,
                100.0f * DESPREAD_SYNC_THRESHOLD);
        free(chip_i); free(chip_q); free(prn_i); free(prn_q); free(npi); free(npq);
        return -1;
    }

    sync->off_i   = best_off_i;
    sync->off_q   = best_off_q;
    sync->phase   = best_phase;
    sync->score_i = best_i;
    sync->score_q = best_q;

    fprintf(stderr,
            "[despread] Synced: off_I=%d (%.1f%%), off_Q=%d (%.1f%%), phase=%d°\n",
            best_off_i, 100.0f * best_i / DESPREAD_PREAMBLE_CHIPS,
            best_off_q, 100.0f * best_q / DESPREAD_PREAMBLE_CHIPS,
            best_phase * 90);

    free(chip_i); free(chip_q); free(prn_i); free(prn_q); free(npi); free(npq);
    return 0;
}

int despread_bits(const float complex *samples, int num_chips,
                  const despread_sync_t *sync,
                  uint8_t *output_bits)
{
    if (samples == NULL || sync == NULL || output_bits == NULL)
        return -1;

    int off_i = sync->off_i;
    int off_q = sync->off_q;
    int phase = sync->phase;

    /* Slice the whole chip buffer. */
    int8_t *chip_i = (int8_t *)malloc((size_t)num_chips);
    int8_t *chip_q = (int8_t *)malloc((size_t)num_chips);
    if (!chip_i || !chip_q) { free(chip_i); free(chip_q); return -1; }

    for (int k = 0; k < num_chips; k++) {
        chip_i[k] = (__real__ samples[k] >= 0.0f) ? 1 : 0;
        chip_q[k] = (__imag__ samples[k] >= 0.0f) ? 1 : 0;
    }

    /* Full-length PRN for message despreading. */
    int8_t *prn_i = (int8_t *)malloc(DESPREAD_PRN_LEN);
    int8_t *prn_q = (int8_t *)malloc(DESPREAD_PRN_LEN);
    if (!prn_i || !prn_q) {
        free(chip_i); free(chip_q); free(prn_i); free(prn_q);
        return -1;
    }
    despread_gen_prn(DESPREAD_PRN_SEED_I, DESPREAD_PRN_LEN, prn_i);
    despread_gen_prn(DESPREAD_PRN_SEED_Q, DESPREAD_PRN_LEN, prn_q);

    int out_idx = 0;
    for (int k = 0; k < DESPREAD_TOTAL_BITS; k++) {
        int cs_i = off_i + k * DESPREAD_CHIPS_PER_BIT;
        int cs_q = off_q + k * DESPREAD_CHIPS_PER_BIT;
        if (cs_i + DESPREAD_CHIPS_PER_BIT > num_chips ||
            cs_q + DESPREAD_CHIPS_PER_BIT > num_chips)
            break;

        const int8_t *ci = chip_i + cs_i;
        const int8_t *cq = chip_q + cs_q;
        const int8_t *pi = prn_i + k * DESPREAD_CHIPS_PER_BIT;
        const int8_t *pq = prn_q + k * DESPREAD_CHIPS_PER_BIT;

        int m_i, m_q;
        uint8_t d_i, d_q;
        switch (phase) {
        case 0:
            m_i = count_matches(ci, pi, DESPREAD_CHIPS_PER_BIT);
            m_q = count_matches(cq, pq, DESPREAD_CHIPS_PER_BIT);
            d_i = (m_i > 128) ? 1 : 0;
            d_q = (m_q > 128) ? 1 : 0;
            break;
        case 1:
            m_i = count_matches(cq, pi, DESPREAD_CHIPS_PER_BIT);
            m_q = count_matches(ci, pq, DESPREAD_CHIPS_PER_BIT);
            d_i = (m_i > 128) ? 1 : 0;
            d_q = (m_q > 128) ? 0 : 1;
            break;
        case 2:
            m_i = count_matches(ci, pi, DESPREAD_CHIPS_PER_BIT);
            m_q = count_matches(cq, pq, DESPREAD_CHIPS_PER_BIT);
            d_i = (m_i > 128) ? 0 : 1;
            d_q = (m_q > 128) ? 0 : 1;
            break;
        default: /* 3 (270°) */
            m_i = count_matches(cq, pi, DESPREAD_CHIPS_PER_BIT);
            m_q = count_matches(ci, pq, DESPREAD_CHIPS_PER_BIT);
            d_i = (m_i > 128) ? 0 : 1;
            d_q = (m_q > 128) ? 1 : 0;
            break;
        }

        if (k >= DESPREAD_PREAMBLE_BITS && out_idx + 2 <= DESPREAD_OUTPUT_BITS) {
            output_bits[out_idx]     = d_i;
            output_bits[out_idx + 1] = d_q;
            out_idx += 2;
        }
    }

    free(chip_i); free(chip_q); free(prn_i); free(prn_q);
    return (out_idx == DESPREAD_OUTPUT_BITS) ? 0 : -1;
}

int despread_burst(const float complex *samples, int num_chips,
                   uint8_t *output_bits)
{
    despread_sync_t sync;
    if (despread_sync(samples, num_chips, &sync) != 0)
        return -1;
    return despread_bits(samples, num_chips, &sync, output_bits);
}
