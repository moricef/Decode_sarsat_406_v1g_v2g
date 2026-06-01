/**
 * @file test_sgb_codec.c
 * @brief Unit tests for SGB codec — ported from sgb-codec Python test suite
 *
 * Covers:
 *   M2 — BCH(250,202) encode / decode / error correction
 *   M3 — PRN LFSR generator (23-bit, polynomial X^23 + X^18 + 1)
 *   M4 — Message build / parse (Appendix B.1, C, rotating fields, 23-hex ID)
 *
 * Ground truth vectors extracted from:
 *   sgb-codec/test_vectors/bch_polynomial.json
 *   sgb-codec/test_vectors/bch_vector.json
 *   sgb-codec/test_vectors/prn_sequences.json
 *
 * Build:  gcc -std=c11 -Wall -Wextra -O2 -o test_sgb_codec test_sgb_codec.c -lm
 * Run:    ./test_sgb_codec
 *
 * C/S T.018 Rev 7 / Oct 2024 — Reference specification
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* =====================================================================
 * Test harness
 * ===================================================================== */

static int g_pass = 0;
static int g_fail = 0;

static void check_int(const char *name, int64_t got, int64_t expected)
{
    if (got == expected) {
        g_pass++;
        printf("  [PASS] %s\n", name);
    } else {
        g_fail++;
        printf("  [FAIL] %s: expected %lld, got %lld\n", name,
               (long long)expected, (long long)got);
    }
}

static void check_str(const char *name, const char *got, const char *expected)
{
    if (strcmp(got, expected) == 0) {
        g_pass++;
        printf("  [PASS] %s\n", name);
    } else {
        g_fail++;
        printf("  [FAIL] %s: expected '%s', got '%s'\n", name, expected, got);
    }
}

static void check_bool(const char *name, int got, int expected)
{
    check_int(name, got ? 1 : 0, expected ? 1 : 0);
}

static void check_double(const char *name, double got, double expected, double tol)
{
    if (fabs(got - expected) <= tol) {
        g_pass++;
        printf("  [PASS] %s\n", name);
    } else {
        g_fail++;
        printf("  [FAIL] %s: expected %.6f, got %.6f\n", name, expected, got);
    }
}


/* =====================================================================
 * Bit manipulation helpers (MSB-first convention)
 * ===================================================================== */

/* Convert hex string (e.g. "00E6...") to MSB-first bit array.
 * Returns number of bits written (hex_len * 4). */
static int hex_to_bits(const char *hex, uint8_t *bits, int max_bits)
{
    int hex_len = (int)strlen(hex);
    int bit_count = 0;
    for (int i = 0; i < hex_len && bit_count < max_bits; i++) {
        char c = hex[i];
        int val = (c >= '0' && c <= '9') ? (c - '0')
                : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)
                : (c >= 'a' && c <= 'f') ? (c - 'a' + 10) : 0;
        for (int b = 3; b >= 0 && bit_count < max_bits; b--) {
            bits[bit_count++] = (val >> b) & 1;
        }
    }
    return bit_count;
}

/* Convert MSB-first bit array to hex string.
 * The output buffer must be at least (num_bits + 3)/4 + 1 bytes. */
static void bits_to_hex(const uint8_t *bits, int num_bits, char *out)
{
    int hex_chars = (num_bits + 3) / 4;
    for (int i = 0; i < hex_chars; i++) {
        uint8_t nib = 0;
        for (int j = 0; j < 4; j++) {
            int idx = i * 4 + j;
            nib = (uint8_t)((nib << 1) | ((idx < num_bits) ? bits[idx] : 0));
        }
        out[i] = "0123456789ABCDEF"[nib];
    }
    out[hex_chars] = '\0';
}

/* Extract bits[start..start+len-1] as unsigned (MSB-first). */
static uint64_t get_bits(const uint8_t *bits, int start, int len)
{
    uint64_t val = 0;
    for (int i = 0; i < len; i++)
        val = (val << 1) | bits[start + i];
    return val;
}

/* Write value to bits[start..start+len-1] (MSB-first). */
static void put_bits(uint8_t *bits, int start, int len, uint64_t value)
{
    for (int i = len - 1; i >= 0; i--) {
        bits[start + i] = value & 1;
        value >>= 1;
    }
}


/* =====================================================================
 * BCH(250,202) — Generator polynomial (T.018 Appendix B)
 * ===================================================================== */

/* 49-bit generator: 1110001111110101110000101110111110011110010010111 */
static const uint64_t BCH_GEN = 0x1C7EB85DF3C97ULL;
#define BCH_GEN_DEGREE 48

#define BCH_INFO  202
#define BCH_PARITY 48
#define BCH_TOTAL  250

/* ---------- BCH encode: 202 info bits → 48 parity bits (MSB-first) --------- */
static void bch_encode(const uint8_t *info, uint8_t *parity)
{
    uint64_t rem = 0;
    for (int i = 0; i < BCH_INFO; i++) {
        rem = (rem << 1) | info[i];
        if (rem & (1ULL << BCH_GEN_DEGREE))
            rem ^= BCH_GEN;
    }
    for (int i = 0; i < BCH_PARITY; i++) {
        rem <<= 1;
        if (rem & (1ULL << BCH_GEN_DEGREE))
            rem ^= BCH_GEN;
    }
    for (int i = 0; i < BCH_PARITY; i++)
        parity[i] = (rem >> (BCH_PARITY - 1 - i)) & 1;
}

/* ---------- Compute syndrome (non-zero = errors) ---------- */
static uint64_t bch_syndrome(const uint8_t *codeword_250)
{
    uint64_t rem = 0;
    /* Build 255-bit message: 5 leading zeros + 250 codeword bits */
    for (int i = 0; i < 5; i++) {
        rem = (rem << 1);
        if (rem & (1ULL << BCH_GEN_DEGREE)) rem ^= BCH_GEN;
    }
    for (int i = 0; i < BCH_TOTAL; i++) {
        rem = (rem << 1) | codeword_250[i];
        if (rem & (1ULL << BCH_GEN_DEGREE)) rem ^= BCH_GEN;
    }
    return rem;
}

/* ---------- BCH decode with error correction (up to 6 bits) ---------- */
/* GF(2^8) tables for syndrome evaluation. Primitive poly: X^8+X^4+X^3+X^2+1 */
#define GF_ORDER 256
static uint8_t gf_exp[512];
static uint8_t gf_log[256];
static int gf_ready = 0;

static void gf_init(void)
{
    if (gf_ready) return;
    uint16_t x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;  /* X^8+X^4+X^3+X^2+1 */
    }
    for (int i = 255; i < 512; i++)
        gf_exp[i] = gf_exp[i - 255];
    gf_ready = 1;
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (!a || !b) return 0;
    return gf_exp[(gf_log[a] + gf_log[b]) % 255];
}

static uint8_t gf_div(uint8_t a, uint8_t b)
{
    if (!a) return 0;
    return gf_exp[(gf_log[a] - gf_log[b] + 255) % 255];
}

/* Evaluate codeword polynomial at alpha^pow in GF(2^8).
 * bits[0] is coefficient of X^(n-1). */
static uint8_t eval_at_alpha(const uint8_t *bits, int n, int alpha_pow)
{
    uint8_t result = 0;
    for (int i = 0; i < n; i++) {
        if (bits[i]) {
            int exp = ((n - 1 - i) * alpha_pow) % 255;
            result ^= gf_exp[exp];
        }
    }
    return result;
}

/* Compute syndromes S1..S12 */
static void compute_syndromes(const uint8_t *cw, uint8_t *syn)
{
    for (int i = 1; i <= 12; i++)
        syn[i - 1] = eval_at_alpha(cw, BCH_TOTAL, i);
}

/* Berlekamp-Massey — find error locator polynomial.
 * syn[0..11] = S1..S12. lambda[0] = 1. Returns degree L. */
static int berlekamp_massey(const uint8_t *syn, uint8_t *lambda, int max_deg)
{
    uint8_t lam[32] = {1};
    uint8_t B[32] = {1};
    int L = 0, m = 1;
    uint8_t b = 1;

    for (int n = 0; n < 12; n++) {
        uint8_t delta = syn[n];
        for (int i = 1; i <= L; i++)
            delta ^= gf_mul(lam[i], syn[n - i]);
        if (delta == 0) {
            m++;
        } else if (2 * L <= n) {
            uint8_t T[32];
            memcpy(T, lam, sizeof(T));
            uint8_t coef = gf_div(delta, b);
            for (int i = 0; i < 32; i++) {
                uint8_t sb = (i >= m && B[i - m]) ? gf_mul(coef, B[i - m]) : 0;
                lam[i] ^= sb;
            }
            L = n + 1 - L;
            memcpy(B, T, sizeof(B));
            b = delta;
            m = 1;
        } else {
            uint8_t coef = gf_div(delta, b);
            for (int i = 0; i < 32; i++) {
                uint8_t sb = (i >= m && B[i - m]) ? gf_mul(coef, B[i - m]) : 0;
                lam[i] ^= sb;
            }
            m++;
        }
    }
    /* Trim to actual degree */
    while (L > 0 && lam[L] == 0) L--;
    memcpy(lambda, lam, (size_t)(L + 1));
    return L;
}

/* Chien search — find roots alpha^-i in positions 0..n-1 */
static int chien_search(const uint8_t *lambda, int L, int n, int *positions)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        uint8_t sum = 0;
        for (int j = 0; j <= L; j++) {
            if (lambda[j])
                sum ^= gf_mul(lambda[j], gf_exp[((255 - i) * j) % 255]);
        }
        if (sum == 0) {
            int pos_msb = n - 1 - i;
            if (pos_msb >= 0 && pos_msb < n && count < 12)
                positions[count++] = pos_msb;
        }
    }
    return count;
}

/* Full BCH decode: returns corrected codeword in place, number of errors
 * corrected, and ok flag. */
static int bch_decode(uint8_t *cw, int *errors_out)
{
    uint8_t syn[12];
    compute_syndromes(cw, syn);

    int all_zero = 1;
    for (int i = 0; i < 12; i++) if (syn[i]) { all_zero = 0; break; }
    if (all_zero) { *errors_out = 0; return 1; }

    uint8_t lambda[32] = {0};
    int L = berlekamp_massey(syn, lambda, 12);
    if (L == 0 || L > 6) return 0;

    int pos[12];
    int npos = chien_search(lambda, L, BCH_TOTAL, pos);
    if (npos != L) return 0;

    for (int i = 0; i < npos; i++)
        cw[pos[i]] ^= 1;

    /* Re-verify */
    compute_syndromes(cw, syn);
    all_zero = 1;
    for (int i = 0; i < 12; i++) if (syn[i]) { all_zero = 0; break; }
    if (all_zero) { *errors_out = L; return 1; }
    return 0;
}


/* =====================================================================
 * PRN LFSR — 23-bit, polynomial X^23 + X^18 + 1
 * ===================================================================== */

/* Initial states per T.018 Table 2.2 */
#define PRN_INIT_NORMAL_I    0x000001UL
#define PRN_INIT_NORMAL_Q    0x1AC1FCUL
#define PRN_INIT_SELFTEST_I  0x52C9F0UL
#define PRN_INIT_SELFTEST_Q  0x3CE928UL

#define PRN_REG_WIDTH 23
#define PRN_PERIOD    ((1UL << 23) - 1)  /* 8388607 */

/* Step LFSR once, return output bit (state & 1), update state. */
static int prn_step(uint32_t *state)
{
    int out = *state & 1;
    uint32_t fb = ((*state) ^ (*state >> 18)) & 1;
    *state = (*state >> 1) | (fb << 22);
    return out;
}

/* Generate `n` chips into out[] (each 0 or 1). */
static void prn_gen(uint32_t state, int n, uint8_t *out)
{
    for (int i = 0; i < n; i++)
        out[i] = (uint8_t)prn_step(&state);
}

/* First 64 chips as hex string (16 chars). */
static void prn_first64_hex(uint32_t state, char *out)
{
    uint8_t chips[64];
    prn_gen(state, 64, chips);
    bits_to_hex(chips, 64, out);
}


/* =====================================================================
 * GNSS position encoding (T.018 Appendix C)
 * ===================================================================== */

/* Encode latitude: 1 sign + 7 deg + 15 frac = 23 bits, MSB-first.
 * Pass NaN for no-fix default. */
static void encode_latitude(double lat, uint8_t *bits)
{
    if (isnan(lat)) {
        /* No-fix default per T.018: 1 1111111 000001111100000 */
        put_bits(bits, 0, 23, 0x7F83E0);
        return;
    }
    int sign = (lat < 0) ? 1 : 0;
    double a = fabs(lat);
    int deg = (int)a;
    int frac = (int)((a - deg) * 32768.0 + 0.5);
    bits[0] = sign;
    put_bits(bits, 1, 7, deg);
    put_bits(bits, 8, 15, frac);
}

static double decode_latitude(const uint8_t *bits)
{
    /* Check no-fix default: sign=1, deg=127, frac=0x07C0 */
    int sign = bits[0];
    int deg = (int)get_bits(bits, 1, 7);
    int frac = (int)get_bits(bits, 8, 15);
    /* Default no-fix: sign=1, deg=127, frac=0x3E0 */
    if (sign == 1 && deg == 0x7F && frac == 0x3E0)
        return NAN;
    double lat = deg + frac / 32768.0;
    return sign ? -lat : lat;
}

/* Encode longitude: 1 sign + 8 deg + 15 frac = 24 bits, MSB-first. */
static void encode_longitude(double lon, uint8_t *bits)
{
    if (isnan(lon)) {
        /* No-fix default per T.018: 1 11111111 111110000011111 */
        put_bits(bits, 0, 24, 0xFFFC1F);
        return;
    }
    int sign = (lon < 0) ? 1 : 0;
    double a = fabs(lon);
    int deg = (int)a;
    int frac = (int)((a - deg) * 32768.0 + 0.5);
    bits[0] = sign;
    put_bits(bits, 1, 8, deg);
    put_bits(bits, 9, 15, frac);
}

static double decode_longitude(const uint8_t *bits)
{
    int sign = bits[0];
    int deg = (int)get_bits(bits, 1, 8);
    int frac = (int)get_bits(bits, 9, 15);
    /* Default no-fix: sign=1, deg=255, frac=0x7C1F */
    if (sign == 1 && deg == 0xFF && frac == 0x7C1F)
        return NAN;
    double lon = deg + frac / 32768.0;
    return sign ? -lon : lon;
}


/* =====================================================================
 * 23-hex ID derivation (T.018 Table 3.11 / Appendix B.2)
 * ===================================================================== */
static void derive_23hex_id(const uint8_t *main_bits, char *out)
{
    uint8_t id_bits[92];
    memset(id_bits, 0, sizeof(id_bits));

    id_bits[0] = 1;
    for (int i = 0; i < 10; i++) id_bits[1 + i]  = main_bits[30 + i];
    id_bits[11] = 1; id_bits[12] = 0; id_bits[13] = 1;
    for (int i = 0; i < 16; i++) id_bits[14 + i] = main_bits[i];
    for (int i = 0; i < 14; i++) id_bits[30 + i] = main_bits[16 + i];
    id_bits[44] = main_bits[42];
    for (int i = 0; i < 3; i++) id_bits[45 + i] = main_bits[90 + i];

    /* Vessel ID content: bits 93-136 (1-based 94-137).
     * For ICAO type (100), the last 20 bits are forced to 0
     * per T.018 Appendix B.2 / Table 3.11 note. */
    int vessel_type = (int)get_bits(main_bits, 90, 3);
    for (int i = 0; i < 44; i++) {
        int force_zero = (vessel_type == 4 /* ICAO */ && i >= 24);
        id_bits[48 + i] = force_zero ? 0 : main_bits[93 + i];
    }

    bits_to_hex(id_bits, 92, out);
}


/* =====================================================================
 * Modified Baudot (T.018 Table 3.2) — encode / decode
 * ===================================================================== */
static uint8_t baudot_encode_char(char c)
{
    /* Subset sufficient for callsigns / tail numbers */
    static const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -/";
    static const uint8_t codes[] = {
        0b000001, 0b000010, 0b000011, 0b000100, 0b000101, 0b000110, 0b000111, 0b001000,
        0b001001, 0b001010, 0b001011, 0b001100, 0b001101, 0b001110, 0b001111, 0b010000,
        0b010001, 0b010010, 0b010011, 0b010100, 0b010101, 0b010110, 0b010111, 0b011000,
        0b011001, 0b011010,
        0b011011, 0b011100, 0b011101, 0b011110, 0b011111, 0b100000, 0b100001, 0b100010,
        0b100011, 0b100100,
        0b100100, 0b011000, 0b010111
    };
    c = (char)toupper((unsigned char)c);
    for (int i = 0; chars[i]; i++)
        if (chars[i] == c) return codes[i];
    return 0b100100;  /* space / spare */
}

static char baudot_decode_char(uint8_t code)
{
    static const struct { uint8_t code; char ch; } map[] = {
        {0b000001,'A'},{0b000010,'B'},{0b000011,'C'},{0b000100,'D'},
        {0b000101,'E'},{0b000110,'F'},{0b000111,'G'},{0b001000,'H'},
        {0b001001,'I'},{0b001010,'J'},{0b001011,'K'},{0b001100,'L'},
        {0b001101,'M'},{0b001110,'N'},{0b001111,'O'},{0b010000,'P'},
        {0b010001,'Q'},{0b010010,'R'},{0b010011,'S'},{0b010100,'T'},
        {0b010101,'U'},{0b010110,'V'},{0b010111,'W'},{0b011000,'X'},
        {0b011001,'Y'},{0b011010,'Z'},
        {0b011011,'0'},{0b011100,'1'},{0b011101,'2'},{0b011110,'3'},
        {0b011111,'4'},{0b100000,'5'},{0b100001,'6'},{0b100010,'7'},
        {0b100011,'8'},{0b100100,' '},{0b010111,'/'},{0b011000,'-'},
        {0, 0}
    };
    for (int i = 0; map[i].ch; i++)
        if (map[i].code == code) return map[i].ch;
    return '?';
}


/* =====================================================================
 * Rotating field builders (simplified — only the types needed for tests)
 * ===================================================================== */

/* Build rotating field #0 (G.008 Objective Requirements) — 48 bits */
static void build_rot_g008(uint8_t elapsed_h, uint16_t last_loc_min,
                           int alt_m, uint8_t hdop, uint8_t vdop,
                           uint8_t activation, uint8_t battery,
                           uint8_t gnss, uint8_t *out)
{
    memset(out, 0, 48);
    put_bits(out, 0, 4, 0);          /* identifier */
    put_bits(out, 4, 6, elapsed_h);
    put_bits(out, 10, 11, last_loc_min);
    /* altitude: code = (alt + 400) / 16 */
    uint16_t ac = (uint16_t)((alt_m + 400) / 16);
    put_bits(out, 21, 10, ac);
    put_bits(out, 31, 4, hdop);
    put_bits(out, 35, 4, vdop);
    put_bits(out, 39, 2, activation);
    put_bits(out, 41, 3, battery);
    put_bits(out, 44, 2, gnss);
}

/* Build rotating field #1 (ELT-DT In-Flight) — 48 bits */
static void build_rot_elt_dt(uint32_t tll_s, int alt_m,
                             uint8_t trigger, uint8_t gnss,
                             uint8_t battery, uint8_t *out)
{
    memset(out, 0, 48);
    put_bits(out, 0, 4, 1);          /* identifier */
    put_bits(out, 4, 17, tll_s);
    uint16_t ac = (uint16_t)((alt_m + 400) / 16);
    put_bits(out, 21, 10, ac);
    put_bits(out, 31, 4, trigger);
    put_bits(out, 35, 2, gnss);
    put_bits(out, 37, 2, battery);
}


/* =====================================================================
 * MAIN — Test suites
 * ===================================================================== */

int main(void)
{
    gf_init();
    srand((unsigned)time(NULL));

    printf("===== SGB CODEC UNIT TESTS (C port) =====\n\n");

    /* =================================================================
     * M2 — BCH(250,202)
     * ================================================================= */
    printf("--- M2: BCH(250,202) ---\n");

    /* [1] Generator polynomial match */
    printf("\n[1] Generator polynomial\n");
    {
        const char *spec_poly = "1110001111110101110000101110111110011110010010111";
        /* Verify our constant matches spec bit string */
        int match = 1;
        for (int i = 0; i <= 48; i++) {
            int spec_bit = spec_poly[48 - i] - '0';
            int code_bit = (int)((BCH_GEN >> i) & 1);
            if (spec_bit != code_bit) match = 0;
        }
        check_bool("generator polynomial matches spec", match, 1);
    }

    /* [2] Appendix B.1 parity */
    printf("\n[2] Appendix B.1 BCH parity\n");
    {
        const char *main_hex = "00E608F4C986196188A047C000000000000FFFC0100C1A00960";
        const char *exp_parity_bin = "010010010010101001001111110001010111101001001001";
        uint8_t main_bits[202];
        hex_to_bits(main_hex, main_bits, 202);

        uint8_t parity[48];
        bch_encode(main_bits, parity);

        /* Compare bit-by-bit with expected binary string */
        int ok = 1;
        for (int i = 0; i < 48; i++)
            if (parity[i] != (uint8_t)(exp_parity_bin[i] - '0')) ok = 0;
        check_bool("B.1 parity bits match", ok, 1);

        char par_hex[13];
        bits_to_hex(parity, 48, par_hex);
        check_str("B.1 parity hex = 492A4FC57A49", par_hex, "492A4FC57A49");
    }

    /* [3] Zero-error decode round-trip */
    printf("\n[3] Zero-error decode (50 trials)\n");
    {
        int ok = 1;
        for (int t = 0; t < 50 && ok; t++) {
            uint8_t info[BCH_INFO], codeword[BCH_TOTAL], parity[BCH_PARITY];
            for (int i = 0; i < BCH_INFO; i++)
                info[i] = (uint8_t)(rand() & 1);
            bch_encode(info, parity);
            memcpy(codeword, info, BCH_INFO);
            memcpy(codeword + BCH_INFO, parity, BCH_PARITY);

            /* Syndrome must be zero */
            if (bch_syndrome(codeword) != 0) { ok = 0; break; }

            /* Decode must return no errors */
            uint8_t cw_copy[BCH_TOTAL];
            memcpy(cw_copy, codeword, BCH_TOTAL);
            int errors = -1;
            int dec_ok = bch_decode(cw_copy, &errors);
            if (!dec_ok || errors != 0) { ok = 0; break; }
            if (memcmp(cw_copy, codeword, BCH_TOTAL) != 0) { ok = 0; break; }
        }
        check_bool("zero-error decode 50x", ok, 1);
    }

    /* [4] 1..6 bit error correction */
    printf("\n[4] Random 1-6 bit error correction (30 trials/level)\n");
    for (int t = 1; t <= 6; t++) {
        int ok = 1;
        for (int trial = 0; trial < 30 && ok; trial++) {
            uint8_t info[BCH_INFO], codeword[BCH_TOTAL], parity[BCH_PARITY];
            for (int i = 0; i < BCH_INFO; i++)
                info[i] = (uint8_t)(rand() & 1);
            bch_encode(info, parity);
            memcpy(codeword, info, BCH_INFO);
            memcpy(codeword + BCH_INFO, parity, BCH_PARITY);

            /* Flip t random bits */
            uint8_t corrupted[BCH_TOTAL];
            memcpy(corrupted, codeword, BCH_TOTAL);
            int flipped[250];
            for (int i = 0; i < 250; i++) flipped[i] = i;
            for (int i = 0; i < 250 - 1; i++) {
                int j = i + rand() % (250 - i);
                int tmp = flipped[i];
                flipped[i] = flipped[j];
                flipped[j] = tmp;
            }
            for (int i = 0; i < t; i++)
                corrupted[flipped[i]] ^= 1;

            int errors = -1;
            int dec_ok = bch_decode(corrupted, &errors);
            if (!dec_ok || errors != t) { ok = 0; break; }
            if (memcmp(corrupted, codeword, BCH_TOTAL) != 0) { ok = 0; break; }
        }
        char name[32];
        snprintf(name, sizeof(name), "decode %d-bit errors", t);
        check_bool(name, ok, 1);
    }

    /* [5] 7-bit error rejection */
    printf("\n[5] 7-bit error rejection (30 trials)\n");
    {
        int refused = 0;
        for (int trial = 0; trial < 30; trial++) {
            uint8_t info[BCH_INFO], codeword[BCH_TOTAL], parity[BCH_PARITY];
            for (int i = 0; i < BCH_INFO; i++)
                info[i] = (uint8_t)(rand() & 1);
            bch_encode(info, parity);
            memcpy(codeword, info, BCH_INFO);
            memcpy(codeword + BCH_INFO, parity, BCH_PARITY);

            uint8_t corrupted[BCH_TOTAL];
            memcpy(corrupted, codeword, BCH_TOTAL);
            int flipped[250];
            for (int i = 0; i < 250; i++) flipped[i] = i;
            for (int i = 0; i < 250 - 1; i++) {
                int j = i + rand() % (250 - i);
                int tmp = flipped[i]; flipped[i] = flipped[j]; flipped[j] = tmp;
            }
            for (int i = 0; i < 7; i++)
                corrupted[flipped[i]] ^= 1;

            int errors = -1;
            int dec_ok = bch_decode(corrupted, &errors);
            if (!dec_ok) refused++;
        }
        printf("         refused %d/30 7-bit patterns (>=21 required)\n", refused);
        check_bool("7-bit patterns mostly rejected", refused >= 21, 1);
    }


    /* =================================================================
     * M3 — PRN LFSR
     * ================================================================= */
    printf("\n--- M3: PRN LFSR (23-bit) ---\n");

    /* [1] First 64 chips for all 4 initial states */
    printf("\n[1] First-64-chip match (Table 2.2)\n");
    {
        const struct {
            uint32_t seed; const char *name; const char *hex;
        } vecs[] = {
            {PRN_INIT_NORMAL_I,   "normal_i",    "80000108421284A1"},
            {PRN_INIT_NORMAL_Q,   "normal_q",    "3F8358BAD030F231"},
            {PRN_INIT_SELFTEST_I, "self_test_i", "0F934A4D4CF3028D"},
            {PRN_INIT_SELFTEST_Q, "self_test_q", "14973DC716CDE124"},
        };
        for (int v = 0; v < 4; v++) {
            char hex[17];
            prn_first64_hex(vecs[v].seed, hex);
            char name[32];
            snprintf(name, sizeof(name), "first 64 chips: %s", vecs[v].name);
            check_str(name, hex, vecs[v].hex);
        }
    }

    /* [2] Segment length = 38400, values 0/1 */
    printf("\n[2] Segment length 38400 chips\n");
    {
        uint8_t *seg = malloc(38400);
        int ok = 1;
        for (int mode = 0; mode < 2 && ok; mode++) {
            uint32_t seeds[2] = { mode ? PRN_INIT_SELFTEST_I : PRN_INIT_NORMAL_I,
                                  mode ? PRN_INIT_SELFTEST_Q : PRN_INIT_NORMAL_Q };
            for (int ch = 0; ch < 2 && ok; ch++) {
                prn_gen(seeds[ch], 38400, seg);
                for (int i = 0; i < 38400; i++)
                    if (seg[i] > 1) { ok = 0; break; }
            }
        }
        free(seg);
        check_bool("38400-chip segments valid", ok, 1);
    }

    /* [3] Register width */
    printf("\n[3] Register width = 23\n");
    check_int("register width", PRN_REG_WIDTH, 23);

    /* [4] Period = 2^23 - 1 — verify state returns to start */
    printf("\n[4] Period = 2^23 - 1\n");
    {
        uint32_t state = PRN_INIT_NORMAL_I;
        for (uint32_t i = 0; i < PRN_PERIOD; i++)
            (void)prn_step(&state);
        check_int("state returns to init after period", (int64_t)state,
                  (int64_t)PRN_INIT_NORMAL_I);
    }

    /* [5] Reset reproduces same sequence */
    printf("\n[5] Reset reproduces same sequence\n");
    {
        uint32_t state = PRN_INIT_NORMAL_I;
        uint8_t a[500], b[500];
        prn_gen(state, 500, a);
        state = PRN_INIT_NORMAL_I;
        prn_gen(state, 500, b);
        int ok = (memcmp(a, b, 500) == 0);
        check_bool("reset + regenerate matches", ok, 1);
    }


    /* =================================================================
     * M4 — Message build / parse
     * ================================================================= */
    printf("\n--- M4: Message build/parse ---\n");

    /* [1] Appendix B.1 round-trip */
    printf("\n[1] Appendix B.1 round-trip\n");
    {
        const char *b1_hex = "00E608F4C986196188A047C000000000000FFFC0100C1A00960";
        const char *b1_23hex = "9934039823D000000000000";
        uint8_t main_bits[202];
        hex_to_bits(b1_hex, main_bits, 202);

        char hex_out[52];
        bits_to_hex(main_bits, 202, hex_out);
        check_str("main-field hex round-trip", hex_out, b1_hex);

        /* BCH parity */
        uint8_t parity[48];
        bch_encode(main_bits, parity);
        char par_hex[13];
        bits_to_hex(parity, 48, par_hex);
        check_str("B.1 parity hex", par_hex, "492A4FC57A49");

        /* 23-hex ID */
        char id23[24];
        derive_23hex_id(main_bits, id23);
        check_str("23-hex Beacon ID", id23, b1_23hex);

        /* Decode key fields: TAC=230, Serial=573, Country=201 */
        check_int("B.1 TAC", (int64_t)get_bits(main_bits, 0, 16), 230);
        check_int("B.1 Serial", (int64_t)get_bits(main_bits, 16, 14), 573);
        check_int("B.1 Country", (int64_t)get_bits(main_bits, 30, 10), 201);
    }

    /* [2] GNSS encoding (Appendix C) — 48.79315°N, 2.24127°E */
    printf("\n[2] Appendix C GNSS encoding\n");
    {
        uint8_t lat_bits[23], lon_bits[24];
        encode_latitude(48.79315, lat_bits);
        encode_longitude(2.24127, lon_bits);

        check_int("lat sign = 0", lat_bits[0], 0);
        check_int("lat deg = 48", (int)get_bits(lat_bits, 1, 7), 48);
        check_double("lat round-trip", decode_latitude(lat_bits), 48.79315, 1e-4);
        check_int("lon sign = 0", lon_bits[0], 0);
        check_int("lon deg = 2", (int)get_bits(lon_bits, 1, 8), 2);
        check_double("lon round-trip", decode_longitude(lon_bits), 2.24127, 1e-4);
    }

    /* [3] No-fix defaults */
    printf("\n[3] No-fix defaults\n");
    {
        uint8_t lat_bits[23], lon_bits[24];
        encode_latitude(NAN, lat_bits);
        encode_longitude(NAN, lon_bits);
        double lat_d = decode_latitude(lat_bits);
        double lon_d = decode_longitude(lon_bits);
        check_bool("no-fix lat → NaN", isnan(lat_d), 1);
        check_bool("no-fix lon → NaN", isnan(lon_d), 1);
    }

    /* [4] Rotating field round-trips */
    printf("\n[4] Rotating field round-trips\n");
    {
        uint8_t rot[48];

        /* G.008 */
        build_rot_g008(3, 42, 1024, 2, 3, 1, 5, 2, rot);
        check_int("G.008 identifier", (int64_t)get_bits(rot, 0, 4), 0);
        check_int("G.008 elapsed_h", (int64_t)get_bits(rot, 4, 6), 3);
        check_int("G.008 tll_min", (int64_t)get_bits(rot, 10, 11), 42);
        check_int("G.008 hdop", (int64_t)get_bits(rot, 31, 4), 2);
        check_int("G.008 vdop", (int64_t)get_bits(rot, 35, 4), 3);
        check_int("G.008 battery", (int64_t)get_bits(rot, 41, 3), 5);
        check_int("G.008 gnss", (int64_t)get_bits(rot, 44, 2), 2);

        /* ELT(DT) */
        build_rot_elt_dt(12345, 8000, 2, 1, 1, rot);
        check_int("ELT(DT) identifier", (int64_t)get_bits(rot, 0, 4), 1);
        check_int("ELT(DT) tll_s", (int64_t)get_bits(rot, 4, 17), 12345);
        check_int("ELT(DT) trigger", (int64_t)get_bits(rot, 31, 4), 2);
        check_int("ELT(DT) gnss", (int64_t)get_bits(rot, 35, 2), 1);

        /* RLS */
        {   /* identifier=2, provider=010, cap_auto=1, cap_manual=1,
               fb1=1, fb2=0, rlm=all-ones */
            memset(rot, 0, 48);
            put_bits(rot, 0, 4, 2);          /* identifier */
            rot[6] = 1; rot[7] = 1;          /* capability bits */
            /* spare bits 8-11 = 0 */
            put_bits(rot, 12, 3, 2);          /* provider = GLONASS */
            rot[15] = 1;                      /* RLM Type 1 received */
            rot[16] = 0;                      /* RLM Type 2 not received */
            for (int i = 0; i < 20; i++) rot[17 + i] = 1; /* RLM data */
            /* remainder 0 */
            check_int("RLS identifier", (int64_t)get_bits(rot, 0, 4), 2);
            check_int("RLS cap_auto", rot[6], 1);
            check_int("RLS cap_manual", rot[7], 1);
            check_int("RLS provider", (int64_t)get_bits(rot, 12, 3), 2);
            check_int("RLS fb1", rot[15], 1);
            check_int("RLS fb2", rot[16], 0);
            check_int("RLS rlm[0]", rot[17], 1);
        }

        /* National use (#3) — custom payload */
        {
            memset(rot, 0, 48);
            put_bits(rot, 0, 4, 3);          /* identifier */
            for (int i = 0; i < 20; i++) rot[4 + i] = 1;
            check_int("National id", (int64_t)get_bits(rot, 0, 4), 3);
            check_int("National payload[0]", rot[4], 1);
            check_int("National payload[19]", rot[23], 1);
            check_int("National payload[20]", rot[24], 0);
        }

        /* Spare (#7) */
        {
            memset(rot, 0, 48);
            put_bits(rot, 0, 4, 7);          /* identifier */
            check_int("Spare identifier 7", (int64_t)get_bits(rot, 0, 4), 7);
        }

        /* Cancellation (#15) — method 10, fixed-ones bits 6-47 */
        {
            memset(rot, 0, 48);
            put_bits(rot, 0, 4, 15);          /* identifier */
            put_bits(rot, 4, 2, 2);           /* method = 10 */
            for (int i = 6; i < 48; i++) rot[i] = 1; /* fixed all-ones */
            check_int("Cancel id", (int64_t)get_bits(rot, 0, 4), 15);
            check_int("Cancel method", (int64_t)get_bits(rot, 4, 2), 2);
            check_int("Cancel fixed[6]", rot[6], 1);
            check_int("Cancel fixed[47]", rot[47], 1);
        }
    }

    /* [5] GNSS hemisphere round-trips */
    printf("\n[5] GNSS hemisphere round-trips\n");
    {
        double lats[] = {0.0, 45.5, -45.5, 89.99, -89.99};
        double lons[] = {0.0, 90.0, -90.0, 179.99, -179.99};
        for (int i = 0; i < 5; i++) {
            uint8_t lb[23];
            encode_latitude(lats[i], lb);
            char name[32];
            snprintf(name, sizeof(name), "lat %.2f round-trip", lats[i]);
            check_double(name, decode_latitude(lb), lats[i], 1e-3);
        }
        for (int i = 0; i < 5; i++) {
            uint8_t lb[24];
            encode_longitude(lons[i], lb);
            char name[32];
            snprintf(name, sizeof(name), "lon %.2f round-trip", lons[i]);
            check_double(name, decode_longitude(lb), lons[i], 1e-3);
        }
    }

    /* [6] Vessel ID round-trips */
    printf("\n[6] Vessel ID round-trips\n");
    {
        /* MMSI: type=001, 30-bit MMSI + 14-bit AIS suffix */
        {
            uint8_t vid[44];
            memset(vid, 0, 44);
            /* type = 001 */
            put_bits(vid, 0, 30, 366123456UL);   /* MMSI */
            put_bits(vid, 30, 14, 0);             /* AIS suffix = 0 */
            check_int("MMSI value", (int64_t)get_bits(vid, 0, 30), 366123456);
            check_int("MMSI AIS suffix", (int64_t)get_bits(vid, 30, 14), 0);
        }
        /* Callsign: type=010, 7 chars × 6 bits = 42 bits, left-justified */
        {
            uint8_t vid[44];
            memset(vid, 0, 44);
            const char *cs = "WDE3456";
            for (int i = 0; i < 7; i++) {
                uint8_t code = baudot_encode_char(cs[i]);
                put_bits(vid, i * 6, 6, code);
            }
            /* bits 42-43 spare = 00 */
            char decoded[8] = {0};
            for (int i = 0; i < 7; i++)
                decoded[i] = baudot_decode_char((uint8_t)get_bits(vid, i * 6, 6));
            check_str("callsign round-trip", decoded, "WDE3456");
        }
        /* Tail number (right-justified, 7 chars per T.018 Table 3.1 note 3) */
        {
            uint8_t vid[44];
            memset(vid, 0, 44);
            const char *tail = "N12345";
            int tlen = (int)strlen(tail);
            int pad_left = 7 - tlen;  /* spaces on left for right-justify */
            for (int i = 0; i < pad_left; i++) {
                put_bits(vid, i * 6, 6, 0b100100);  /* baudot space */
            }
            for (int i = 0; i < tlen; i++) {
                uint8_t code = baudot_encode_char(tail[i]);
                put_bits(vid, (pad_left + i) * 6, 6, code);
            }
            char decoded[8] = {0};
            for (int i = 0; i < 7; i++)
                decoded[i] = baudot_decode_char((uint8_t)get_bits(vid, i * 6, 6));
            check_str("tail number round-trip", decoded, " N12345");
        }
        /* ICAO 24-bit address (no operator) */
        {
            uint8_t vid[44];
            memset(vid, 0, 44);
            put_bits(vid, 0, 24, 0xABC123);
            /* bits 24-43 spare = 0 */
            check_int("ICAO address", (int64_t)get_bits(vid, 0, 24), 0xABC123);
        }
        /* Operator + serial: 3-letter operator uses 5-bit alphabet index
         * (A=0, B=1, ..., Z=25), NOT Modified Baudot. */
        {
            uint8_t vid[44];
            memset(vid, 0, 44);
            const char *op = "UAL";
            for (int i = 0; i < 3; i++) {
                put_bits(vid, i * 5, 5, (uint64_t)(op[i] - 'A'));
            }
            put_bits(vid, 15, 12, 77);        /* serial */
            for (int i = 27; i < 44; i++) vid[i] = 1; /* spare = all 1s */
            char dec_op[4] = {0};
            for (int i = 0; i < 3; i++)
                dec_op[i] = (char)('A' + (int)get_bits(vid, i * 5, 5));
            check_str("operator code", dec_op, "UAL");
            check_int("operator serial", (int64_t)get_bits(vid, 15, 12), 77);
        }
        /* None type (all zeros) */
        {
            uint8_t vid[44];
            memset(vid, 0, 44);
            int all_zero = 1;
            for (int i = 0; i < 44; i++) if (vid[i]) all_zero = 0;
            check_bool("vessel ID none = all zeros", all_zero, 1);
        }
    }

    /* [7] Full message build/parse round-trip */
    printf("\n[7] Full message build/parse round-trip\n");
    {
        /* Build a complete 202-bit main field */
        uint8_t main[202];
        memset(main, 0, 202);

        put_bits(main, 0, 16, 230);          /* TAC */
        put_bits(main, 16, 14, 1234);        /* Serial */
        put_bits(main, 30, 10, 366);         /* Country (USA) */
        main[40] = 1;                         /* Homing */
        main[41] = 1;                         /* RLS */
        main[42] = 0;                         /* Test = normal */

        /* Position: 48.79315°N, 2.24127°E */
        {
            uint8_t lat[23], lon[24];
            encode_latitude(48.79315, lat);
            encode_longitude(2.24127, lon);
            memcpy(main + 43, lat, 23);
            memcpy(main + 66, lon, 24);
        }

        put_bits(main, 90, 3, 4);            /* Vessel type = ICAO */
        put_bits(main, 93, 24, 0xABC123);     /* ICAO address */
        /* rest of vessel ID = 0 */
        put_bits(main, 137, 3, 3);           /* Beacon type = ELT(DT) */
        for (int i = 140; i < 154; i++) main[i] = 1; /* spare = all 1s */

        /* Rotating field #1 (ELT-DT) */
        {
            uint8_t rot[48];
            build_rot_elt_dt(1000, 5000, 1, 0, 3, rot);
            memcpy(main + 154, rot, 48);
        }

        /* Verify key fields */
        check_int("msg TAC", (int64_t)get_bits(main, 0, 16), 230);
        check_int("msg Serial", (int64_t)get_bits(main, 16, 14), 1234);
        check_int("msg Country", (int64_t)get_bits(main, 30, 10), 366);
        check_int("msg Homing", main[40], 1);
        check_int("msg RLS", main[41], 1);
        check_int("msg Test", main[42], 0);
        check_double("msg lat", decode_latitude(main + 43), 48.79315, 1e-3);
        check_double("msg lon", decode_longitude(main + 66), 2.24127, 1e-3);
        check_int("msg vessel type", (int64_t)get_bits(main, 90, 3), 4);
        check_int("msg ICAO addr", (int64_t)get_bits(main, 93, 24), 0xABC123);
        check_int("msg beacon type", (int64_t)get_bits(main, 137, 3), 3);
        check_int("msg rotating id", (int64_t)get_bits(main, 154, 4), 1);

        /* BCH round-trip */
        uint8_t parity[48];
        bch_encode(main, parity);
        uint8_t codeword[BCH_TOTAL];
        memcpy(codeword, main, BCH_INFO);
        memcpy(codeword + BCH_INFO, parity, BCH_PARITY);
        int errors = -1;
        uint8_t dec[BCH_TOTAL];
        memcpy(dec, codeword, BCH_TOTAL);
        int ok = bch_decode(dec, &errors);
        check_bool("BCH round-trip ok", ok, 1);
        check_int("BCH round-trip errors", errors, 0);
        check_bool("BCH round-trip codeword intact",
                   memcmp(dec, codeword, BCH_TOTAL) == 0, 1);
    }

    /* [8] ICAO 23-hex ID special rule (Appendix B.2)
     * Two messages with same ICAO but different operator codes
     * must produce identical 23-hex IDs (last 20 bits forced to 0). */
    printf("\n[8] ICAO 23-hex ID special rule\n");
    {
        uint8_t msg_a[202], msg_b[202];
        memset(msg_a, 0, 202);
        memset(msg_b, 0, 202);

        /* Common fields */
        put_bits(msg_a, 0, 16, 230); put_bits(msg_b, 0, 16, 230);
        put_bits(msg_a, 16, 14, 1234); put_bits(msg_b, 16, 14, 1234);
        put_bits(msg_a, 30, 10, 366); put_bits(msg_b, 30, 10, 366);
        /* position: same for both */
        {
            uint8_t lat[23], lon[24];
            encode_latitude(48.79315, lat);
            encode_longitude(2.24127, lon);
            memcpy(msg_a + 43, lat, 23); memcpy(msg_b + 43, lat, 23);
            memcpy(msg_a + 66, lon, 24); memcpy(msg_b + 66, lon, 24);
        }
        put_bits(msg_a, 90, 3, 4); put_bits(msg_b, 90, 3, 4);  /* ICAO */
        put_bits(msg_a, 93, 24, 0xABC123); put_bits(msg_b, 93, 24, 0xABC123);
        /* Different operator codes in last 20 bits of vessel ID */
        /* Operator "DAL" vs "SWA": 3×5 bits + 5 spare */
        {
            const char *d = "DAL";
            for (int i = 0; i < 3; i++) {
                put_bits(msg_a + 117, i * 5, 5, baudot_encode_char(d[i]) & 0x1F);
            }
            const char *s = "SWA";
            for (int i = 0; i < 3; i++) {
                put_bits(msg_b + 117, i * 5, 5, baudot_encode_char(s[i]) & 0x1F);
            }
        }
        put_bits(msg_a, 137, 3, 3); put_bits(msg_b, 137, 3, 3); /* ELT(DT) */

        char id_a[24], id_b[24];
        derive_23hex_id(msg_a, id_a);
        derive_23hex_id(msg_b, id_b);
        check_str("ICAO 23-hex ID identical (last 20 forced 0)", id_a, id_b);
    }

    /* =================================================================
     * Summary
     * ================================================================= */
    printf("\n===== RESULTS: %d passed, %d failed =====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
