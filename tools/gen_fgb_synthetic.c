/* gen_fgb_synthetic.c
 *
 * Generate a CS-T.001-compliant FGB synthetic IQ baseband for decoder testing.
 *
 * Logic ported verbatim from the dsPIC firmware
 *   SARSAT_T001_dsPIC33CK_WOPOST.X/protocol_data.{c,h}
 *   SARSAT_T001_dsPIC33CK_WOPOST.X/signal_processor.{c,h}
 *
 * Reproduces what the 5W exercise beacon emits, at baseband:
 *   - 160 ms unmodulated carrier (CW preamble)
 *   - 144-bit T.001 frame in biphase-L PSK at 400 baud
 *   - I-channel only (Q = 0), matching the dsPIC ADL5375 single-channel drive
 *
 * Output: <basename>.sigmf-data (cf32_le, 2.4576 Msps) + .sigmf-meta
 *
 * GPS position is hardcoded to TEST_LATITUDE/LONGITUDE so the synthetic
 * is bit-deterministic (no NMEA dependency).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>

/* === T.001 protocol constants (from dsPIC protocol_data.h) === */
#define MESSAGE_BITS 144
#define BCH1_POLY       0x26D9E3
#define BCH1_POLY_MASK  0x3FFFFF
#define BCH1_DEGREE     21
#define BCH1_DATA_BITS  61
#define BCH2_POLY       0x1539
#define BCH2_POLY_MASK  0x1FFF
#define BCH2_DEGREE     12
#define BCH2_DATA_BITS  26
#define PROTOCOL_ELT_DT 0x9
#define SYNC_SELF_TEST  0x0D0
#define COUNTRY_CODE_FRANCE 227

#define CS_BIT(bit) ((bit) - 1)

/* Frame field positions (from protocol_data.h) */
#define FRAME_PREAMBLE_START 1
#define FRAME_PREAMBLE_LENGTH 15
#define FRAME_SYNC_START 16
#define FRAME_SYNC_LENGTH 9
#define FRAME_FORMAT_FLAG_BIT 25
#define FRAME_PROTOCOL_FLAG_BIT 26
#define FRAME_COUNTRY_START 27
#define FRAME_COUNTRY_LENGTH 10
#define FRAME_PROTOCOL_START 37
#define FRAME_PROTOCOL_LENGTH 4
#define FRAME_BEACON_ID_START 41
#define FRAME_BEACON_ID_LENGTH 26
#define FRAME_POSITION_START 67
#define FRAME_POSITION_LENGTH 19
#define FRAME_BCH1_START 86
#define FRAME_BCH1_LENGTH 21
#define FRAME_ACTIVATION_START 107
#define FRAME_ACTIVATION_LENGTH 2
#define FRAME_ALTITUDE_START 109
#define FRAME_ALTITUDE_LENGTH 4
#define FRAME_FRESHNESS_START 113
#define FRAME_FRESHNESS_LENGTH 2
#define FRAME_OFFSET_START 115
#define FRAME_OFFSET_LENGTH 18
#define FRAME_BCH2_START 133
#define FRAME_BCH2_LENGTH 12

/* === Output configuration === */
#define OUT_SAMPLE_RATE    2457600   /* 2.4576 Msps, native dec406_scan rate */
#define SYMBOL_RATE_HZ     400
#define SAMPLES_PER_BIT    (OUT_SAMPLE_RATE / SYMBOL_RATE_HZ)        /* 6144 */
#define CARRIER_MS         160
#define CARRIER_SAMPLES    ((OUT_SAMPLE_RATE / 1000) * CARRIER_MS)   /* 393216 */
#define PRE_SILENCE_MS     100
#define POST_SILENCE_MS    100
#define PRE_SILENCE_SAMPLES  ((OUT_SAMPLE_RATE / 1000) * PRE_SILENCE_MS)
#define POST_SILENCE_SAMPLES ((OUT_SAMPLE_RATE / 1000) * POST_SILENCE_MS)

/* T.001 phase-modulation depth: ±1.1 rad */
#define PHASE_SHIFT_RADIANS 1.1f

/* === Bit-field operations (verbatim from dsPIC protocol_data.c) === */
static void set_bit_field(uint8_t *frame, uint16_t cs_start_bit, uint8_t length,
                          uint64_t value) {
    for (uint8_t i = 0; i < length; i++)
        frame[CS_BIT(cs_start_bit + i)] = (value >> (length - 1 - i)) & 1;
}

static uint64_t get_bit_field(const uint8_t *frame, uint16_t cs_start_bit,
                              uint8_t length) {
    uint64_t value = 0;
    for (uint8_t i = 0; i < length; i++)
        value = (value << 1) | frame[CS_BIT(cs_start_bit + i)];
    return value;
}

/* === BCH encoder (verbatim from dsPIC protocol_data.c) === */
static uint32_t compute_bch(uint64_t data, int num_bits, uint32_t poly,
                            int poly_degree, uint32_t poly_mask) {
    uint32_t reg = 0;
    uint32_t poly_val = poly & ((1UL << (poly_degree + 1)) - 1);
    for (int i = num_bits - 1; i >= 0; i--) {
        uint8_t bit = (data >> i) & 1;
        uint8_t msb = (reg >> (poly_degree - 1)) & 1;
        reg = ((reg << 1) | bit) & poly_mask;
        if (msb) reg ^= poly_val;
    }
    for (int i = 0; i < poly_degree; i++) {
        uint8_t msb = (reg >> (poly_degree - 1)) & 1;
        reg = (reg << 1) & poly_mask;
        if (msb) reg ^= poly_val;
    }
    return reg;
}

static uint32_t compute_bch1(uint64_t data) {
    return compute_bch(data, BCH1_DATA_BITS, BCH1_POLY, BCH1_DEGREE,
                       BCH1_POLY_MASK);
}

static uint16_t compute_bch2(uint32_t data) {
    return (uint16_t)compute_bch((uint64_t)data, BCH2_DATA_BITS, BCH2_POLY,
                                 BCH2_DEGREE, BCH2_POLY_MASK);
}

/* === Frame construction (adapted from dsPIC build_compliant_frame) === */
/* GPS encoding is hardcoded to TEST_LATITUDE/LONGITUDE — no NMEA dep. */
#define TEST_FINE_POSITION_19BIT 0x12345UL
#define TEST_OFFSET_POSITION_18BIT 0x06789UL
#define TEST_BEACON_ID 0x123456UL
#define TEST_ALTITUDE_CODE 0x2  /* 1200-1600 m bucket */

static void build_frame(uint8_t frame[MESSAGE_BITS]) {
    memset(frame, 0, MESSAGE_BITS);

    /* Bit-sync preamble (15 ones) + frame-sync pattern */
    set_bit_field(frame, FRAME_PREAMBLE_START, FRAME_PREAMBLE_LENGTH, 0x7FFFUL);
    set_bit_field(frame, FRAME_SYNC_START, FRAME_SYNC_LENGTH, SYNC_SELF_TEST);

    /* Format flag = 1 (long), Protocol flag = 0 (user protocol) */
    set_bit_field(frame, FRAME_FORMAT_FLAG_BIT, 1, 1UL);
    set_bit_field(frame, FRAME_PROTOCOL_FLAG_BIT, 1, 0UL);

    /* Country, protocol code, beacon ID */
    set_bit_field(frame, FRAME_COUNTRY_START, FRAME_COUNTRY_LENGTH,
                  COUNTRY_CODE_FRANCE);
    set_bit_field(frame, FRAME_PROTOCOL_START, FRAME_PROTOCOL_LENGTH,
                  PROTOCOL_ELT_DT);
    set_bit_field(frame, FRAME_BEACON_ID_START, FRAME_BEACON_ID_LENGTH,
                  TEST_BEACON_ID);

    /* PDF-1 position (fine 30-minute resolution) */
    set_bit_field(frame, FRAME_POSITION_START, FRAME_POSITION_LENGTH,
                  TEST_FINE_POSITION_19BIT);

    /* BCH1 over PDF-1 (bits 25..85) */
    uint64_t pdf1_data = get_bit_field(frame, 25, 61);
    uint32_t bch1 = compute_bch1(pdf1_data);
    set_bit_field(frame, FRAME_BCH1_START, FRAME_BCH1_LENGTH, bch1);

    /* PDF-2 fields */
    set_bit_field(frame, FRAME_ACTIVATION_START, FRAME_ACTIVATION_LENGTH, 0UL);
    set_bit_field(frame, FRAME_ALTITUDE_START, FRAME_ALTITUDE_LENGTH,
                  TEST_ALTITUDE_CODE);
    set_bit_field(frame, FRAME_FRESHNESS_START, FRAME_FRESHNESS_LENGTH, 2UL);
    set_bit_field(frame, FRAME_OFFSET_START, FRAME_OFFSET_LENGTH,
                  TEST_OFFSET_POSITION_18BIT);

    /* BCH2 over PDF-2 (bits 107..132) */
    uint32_t pdf2_data = (uint32_t)get_bit_field(frame, 107, 26);
    uint16_t bch2 = compute_bch2(pdf2_data);
    set_bit_field(frame, FRAME_BCH2_START, FRAME_BCH2_LENGTH, bch2);
}

/* T.001 strict: I = cos(φ), Q = sin(φ), with φ ∈ {+1.1, -1.1} rad biphase-L. */
static void biphase_l_iq(uint8_t bit, uint32_t sample_in_bit,
                         float *I_out, float *Q_out) {
    uint32_t half = SAMPLES_PER_BIT / 2;
    float phi;
    if (sample_in_bit < half)
        phi = (bit == 1) ? +PHASE_SHIFT_RADIANS : -PHASE_SHIFT_RADIANS;
    else
        phi = (bit == 1) ? -PHASE_SHIFT_RADIANS : +PHASE_SHIFT_RADIANS;
    *I_out = cosf(phi);
    *Q_out = sinf(phi);
}

/* === Frame hex dump (helps debug) === */
static void print_frame_hex(const uint8_t *frame) {
    for (int i = 0; i < MESSAGE_BITS; i++) {
        if (i % 8 == 0 && i) putchar(' ');
        if (i % 36 == 0 && i) putchar('\n');
        putchar(frame[i] ? '1' : '0');
    }
    putchar('\n');
}

/* === Main: build frame, generate IQ, write sigmf === */
int main(int argc, char *argv[]) {
    const char *basename = (argc > 1) ? argv[1]
                                      : "test_fgb_synthetic";
    char data_path[512], meta_path[512];
    snprintf(data_path, sizeof data_path, "%s.sigmf-data", basename);
    snprintf(meta_path, sizeof meta_path, "%s.sigmf-meta", basename);

    /* 1. Build T.001 frame */
    uint8_t frame[MESSAGE_BITS];
    build_frame(frame);

    printf("Frame (144 bits):\n");
    print_frame_hex(frame);

    /* 2. Open output */
    FILE *fp = fopen(data_path, "wb");
    if (!fp) { perror(data_path); return 1; }

    /* 3. Pre-silence */
    float zero_pair[2] = {0.0f, 0.0f};
    for (uint32_t i = 0; i < PRE_SILENCE_SAMPLES; i++)
        fwrite(zero_pair, sizeof(float), 2, fp);

    /* 4. CW preamble: unmodulated carrier on +I axis. */
    float cw_pair[2] = { 1.0f, 0.0f };
    for (uint32_t i = 0; i < CARRIER_SAMPLES; i++)
        fwrite(cw_pair, sizeof(float), 2, fp);

    /* 5. 144-bit biphase-L PSK modulation (T.001 strict). */
    for (int b = 0; b < MESSAGE_BITS; b++) {
        for (uint32_t s = 0; s < SAMPLES_PER_BIT; s++) {
            float iq[2];
            biphase_l_iq(frame[b], s, &iq[0], &iq[1]);
            fwrite(iq, sizeof(float), 2, fp);
        }
    }

    /* 6. Post-silence */
    for (uint32_t i = 0; i < POST_SILENCE_SAMPLES; i++)
        fwrite(zero_pair, sizeof(float), 2, fp);

    fclose(fp);

    uint64_t total_samples = PRE_SILENCE_SAMPLES + CARRIER_SAMPLES
                           + (uint64_t)MESSAGE_BITS * SAMPLES_PER_BIT
                           + POST_SILENCE_SAMPLES;

    /* 7. SigMF metadata */
    FILE *meta = fopen(meta_path, "w");
    if (!meta) { perror(meta_path); return 1; }
    fprintf(meta,
            "{\n"
            "    \"global\": {\n"
            "        \"core:datatype\": \"cf32_le\",\n"
            "        \"core:sample_rate\": %d,\n"
            "        \"core:version\": \"1.0.0\",\n"
            "        \"core:description\": \"CS-T.001 FGB (1G) test frame, biphase-L PSK ±%.1f rad at %d baud, I-channel single-drive (matches dsPIC ADL5375 output)\",\n"
            "        \"core:author\": \"gen_fgb_synthetic (dsPIC port)\",\n"
            "        \"core:hw\": \"Software generated (baseband, no RF)\"\n"
            "    },\n"
            "    \"captures\": [\n"
            "        {\n"
            "            \"core:sample_start\": 0,\n"
            "            \"core:frequency\": 0\n"
            "        }\n"
            "    ],\n"
            "    \"annotations\": [\n"
            "        {\n"
            "            \"core:sample_start\": %u,\n"
            "            \"core:sample_count\": %u,\n"
            "            \"core:comment\": \"CW carrier preamble (%d ms)\"\n"
            "        },\n"
            "        {\n"
            "            \"core:sample_start\": %u,\n"
            "            \"core:sample_count\": %u,\n"
            "            \"core:comment\": \"144-bit T.001 frame, biphase-L at 400 baud\"\n"
            "        }\n"
            "    ]\n"
            "}\n",
            OUT_SAMPLE_RATE,
            (double)PHASE_SHIFT_RADIANS,
            SYMBOL_RATE_HZ,
            (unsigned)PRE_SILENCE_SAMPLES, (unsigned)CARRIER_SAMPLES, CARRIER_MS,
            (unsigned)(PRE_SILENCE_SAMPLES + CARRIER_SAMPLES),
            (unsigned)(MESSAGE_BITS * SAMPLES_PER_BIT));
    fclose(meta);

    printf("\nWrote %s (%llu samples, %.3f s)\n",
           data_path, (unsigned long long)total_samples,
           (double)total_samples / OUT_SAMPLE_RATE);
    printf("Wrote %s\n", meta_path);
    return 0;
}
