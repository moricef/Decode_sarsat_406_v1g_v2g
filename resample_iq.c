/**
 * @file resample_iq.c
 * @brief IQ File Resampler for DSSS/OQPSK Demodulation
 *
 * Resamples IQ files from arbitrary sample rates to 384 kHz for optimal
 * DSSS demodulation (384 kHz / 38.4 kchips/s = 10 samples/chip, exact integer).
 *
 * Uses libsamplerate (Secret Rabbit Code) with highest quality converter
 * (SRC_SINC_BEST_QUALITY) to preserve DSSS preamble correlation.
 *
 * Compile:
 *   gcc -o resample_iq resample_iq.c -lsamplerate -lm -O3
 *
 * Usage:
 *   ./resample_iq <input.iq> <output.iq> <input_rate> [output_rate]
 *
 * Example:
 *   ./resample_iq test_2.5MHz.iq test_384kHz.iq 2500000 384000
 *
 * @author Collaborative development (2025)
 * @license Creative Commons CC BY-NC-SA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <samplerate.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TARGET_SAMPLE_RATE  384000      // Target: 384 kHz (10 sps/chip)
#define CHUNK_SIZE          (256 * 1024) // Process 256k samples at a time

/**
 * @brief Print usage information
 */
static void print_usage(const char *progname) {
    printf("IQ File Resampler for DSSS/OQPSK Demodulation\n");
    printf("==============================================\n\n");
    printf("Usage: %s <input.iq> <output.iq> <input_rate> [output_rate]\n\n", progname);
    printf("Arguments:\n");
    printf("  input.iq     - Input IQ file (complex float32)\n");
    printf("  output.iq    - Output IQ file (resampled)\n");
    printf("  input_rate   - Input sample rate (Hz)\n");
    printf("  output_rate  - Output sample rate (Hz, default: 384000)\n\n");
    printf("Example:\n");
    printf("  %s test_2.5MHz.iq test_384kHz.iq 2500000\n\n", progname);
    printf("Recommended output rates for DSSS demodulation:\n");
    printf("  384000 Hz   - 10.0 sps/chip (exact integer, RECOMMENDED)\n");
    printf("  768000 Hz   - 20.0 sps/chip (exact integer)\n");
    printf("  1536000 Hz  - 40.0 sps/chip (exact integer)\n");
}

/**
 * @brief Resample IQ file using libsamplerate
 *
 * @param input_file Input filename
 * @param output_file Output filename
 * @param input_rate Input sample rate (Hz)
 * @param output_rate Output sample rate (Hz)
 * @return 0 on success, -1 on error
 */
int resample_iq_file(const char *input_file, const char *output_file,
                     float input_rate, float output_rate) {

    // Calculate conversion ratio
    double ratio = (double)output_rate / (double)input_rate;

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("                   IQ FILE RESAMPLER\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Input file:       %s\n", input_file);
    printf("Output file:      %s\n", output_file);
    printf("Input rate:       %.0f Hz\n", input_rate);
    printf("Output rate:      %.0f Hz\n", output_rate);
    printf("Conversion ratio: %.6f\n", ratio);
    printf("Target sps/chip:  %.2f\n", output_rate / 38400.0);
    printf("═══════════════════════════════════════════════════════════\n\n");

    // Open input file
    FILE *fp_in = fopen(input_file, "rb");
    if (!fp_in) {
        fprintf(stderr, "Error: Cannot open input file %s\n", input_file);
        return -1;
    }

    // Get file size
    fseek(fp_in, 0, SEEK_END);
    long file_size = ftell(fp_in);
    fseek(fp_in, 0, SEEK_SET);

    long num_input_samples = file_size / sizeof(float complex);
    long num_output_samples = (long)(num_input_samples * ratio);

    printf("Input samples:    %ld (%.3f sec)\n",
           num_input_samples, num_input_samples / input_rate);
    printf("Output samples:   %ld (%.3f sec)\n\n",
           num_output_samples, num_output_samples / output_rate);

    // Open output file
    FILE *fp_out = fopen(output_file, "wb");
    if (!fp_out) {
        fprintf(stderr, "Error: Cannot create output file %s\n", output_file);
        fclose(fp_in);
        return -1;
    }

    // Create libsamplerate converters (one for I, one for Q)
    int error;
    SRC_STATE *src_i = src_new(SRC_SINC_BEST_QUALITY, 1, &error);
    SRC_STATE *src_q = src_new(SRC_SINC_BEST_QUALITY, 1, &error);

    if (!src_i || !src_q) {
        fprintf(stderr, "Error: Failed to create sample rate converter: %s\n",
                src_strerror(error));
        if (src_i) src_delete(src_i);
        if (src_q) src_delete(src_q);
        fclose(fp_in);
        fclose(fp_out);
        return -1;
    }

    // Allocate buffers
    float complex *input_buf = malloc(CHUNK_SIZE * sizeof(float complex));
    float *input_i = malloc(CHUNK_SIZE * sizeof(float));
    float *input_q = malloc(CHUNK_SIZE * sizeof(float));
    float *output_i = malloc((int)(CHUNK_SIZE * ratio * 1.1) * sizeof(float));
    float *output_q = malloc((int)(CHUNK_SIZE * ratio * 1.1) * sizeof(float));

    if (!input_buf || !input_i || !input_q || !output_i || !output_q) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        src_delete(src_i);
        src_delete(src_q);
        fclose(fp_in);
        fclose(fp_out);
        free(input_buf);
        free(input_i);
        free(input_q);
        free(output_i);
        free(output_q);
        return -1;
    }

    // Process in chunks
    printf("Processing...\n");
    long total_input = 0;
    long total_output = 0;
    int is_last_chunk = 0;

    while (1) {
        // Read chunk
        size_t samples_read = fread(input_buf, sizeof(float complex), CHUNK_SIZE, fp_in);
        if (samples_read == 0) break;

        is_last_chunk = (samples_read < CHUNK_SIZE);

        // Deinterleave I and Q
        for (size_t i = 0; i < samples_read; i++) {
            input_i[i] = crealf(input_buf[i]);
            input_q[i] = cimagf(input_buf[i]);
        }

        // Prepare conversion data
        SRC_DATA src_data_i, src_data_q;

        src_data_i.data_in = input_i;
        src_data_i.input_frames = samples_read;
        src_data_i.data_out = output_i;
        src_data_i.output_frames = (int)(CHUNK_SIZE * ratio * 1.1);
        src_data_i.src_ratio = ratio;
        src_data_i.end_of_input = is_last_chunk;

        src_data_q.data_in = input_q;
        src_data_q.input_frames = samples_read;
        src_data_q.data_out = output_q;
        src_data_q.output_frames = (int)(CHUNK_SIZE * ratio * 1.1);
        src_data_q.src_ratio = ratio;
        src_data_q.end_of_input = is_last_chunk;

        // Convert I channel
        error = src_process(src_i, &src_data_i);
        if (error) {
            fprintf(stderr, "Error: I-channel conversion failed: %s\n",
                    src_strerror(error));
            break;
        }

        // Convert Q channel
        error = src_process(src_q, &src_data_q);
        if (error) {
            fprintf(stderr, "Error: Q-channel conversion failed: %s\n",
                    src_strerror(error));
            break;
        }

        // Verify both channels produced same number of samples
        if (src_data_i.output_frames_gen != src_data_q.output_frames_gen) {
            fprintf(stderr, "Warning: I/Q mismatch: I=%ld, Q=%ld\n",
                    src_data_i.output_frames_gen, src_data_q.output_frames_gen);
        }

        // Interleave and write output
        long frames_to_write = src_data_i.output_frames_gen;
        for (long i = 0; i < frames_to_write; i++) {
            float complex sample = output_i[i] + I * output_q[i];
            fwrite(&sample, sizeof(float complex), 1, fp_out);
        }

        total_input += src_data_i.input_frames_used;
        total_output += frames_to_write;

        // Progress
        int pct = (int)((total_input * 100) / num_input_samples);
        printf("  Progress: %d%% (%ld/%ld samples)    \r",
               pct, total_input, num_input_samples);
        fflush(stdout);

        if (is_last_chunk) break;
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("                    CONVERSION COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Total input:      %ld samples (%.3f sec)\n",
           total_input, total_input / input_rate);
    printf("Total output:     %ld samples (%.3f sec)\n",
           total_output, total_output / output_rate);
    printf("Actual ratio:     %.6f (expected: %.6f)\n",
           (double)total_output / (double)total_input, ratio);
    printf("Output file:      %s\n", output_file);
    printf("═══════════════════════════════════════════════════════════\n\n");

    // Cleanup
    src_delete(src_i);
    src_delete(src_q);
    fclose(fp_in);
    fclose(fp_out);
    free(input_buf);
    free(input_i);
    free(input_q);
    free(output_i);
    free(output_q);

    return 0;
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];
    float input_rate = atof(argv[3]);
    float output_rate = (argc >= 5) ? atof(argv[4]) : TARGET_SAMPLE_RATE;

    // Validate parameters
    if (input_rate <= 0 || output_rate <= 0) {
        fprintf(stderr, "Error: Invalid sample rate\n");
        return 1;
    }

    if (input_rate == output_rate) {
        fprintf(stderr, "Error: Input and output rates are identical (no resampling needed)\n");
        return 1;
    }

    // Perform resampling
    int ret = resample_iq_file(input_file, output_file, input_rate, output_rate);

    if (ret == 0) {
        printf("✓ Resampling successful!\n");
        printf("\nNext steps:\n");
        printf("  1. Verify preamble detection:\n");
        printf("     ./test_sample_rate %s\n", output_file);
        printf("  2. Test demodulation:\n");
        printf("     ./dec406_iq %s\n\n", output_file);
    } else {
        fprintf(stderr, "✗ Resampling failed\n");
    }

    return ret;
}
