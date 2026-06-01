/*
 * Test harness for MATLAB Coder generated dsss_receiver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include "dsss_receiver.h"
#include "dsss_receiver_types.h"
#include "dsss_receiver_terminate.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <input_file.cf32>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    // Open IQ file
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file: %s\n", filename);
        return 1;
    }

    // Allocate buffer for 307200 complex samples
    static creal_T otaBuffer[307200];

    // Read cf32 format (float32 I, float32 Q interleaved)
    float iq_data[307200 * 2];
    size_t samples_read = fread(iq_data, sizeof(float), 307200 * 2, fp);
    fclose(fp);

    if (samples_read != 307200 * 2) {
        fprintf(stderr, "WARNING: Expected 307200 samples, read %zu\n", samples_read / 2);
    }

    // Convert float I/Q to creal_T (double complex)
    for (int i = 0; i < 307200; i++) {
        otaBuffer[i].re = (double)iq_data[2*i];
        otaBuffer[i].im = (double)iq_data[2*i + 1];
    }

    printf("Loaded %zu samples from %s\n", samples_read / 2, filename);
    printf("First sample: %.6f + j%.6f\n", otaBuffer[0].re, otaBuffer[0].im);

    // Configure settings
    struct0_T settings;
    settings.velocity = 0.0;        // No Doppler for this test
    settings.txPower = 1.0;         // Normalized power
    settings.oversampling = 4.0;    // sps = 2 * oversampling = 8

    // Output buffers
    boolean_T rxPayload[202];
    int errs;
    boolean_T rxStatus;

    printf("\n=== Calling MATLAB Coder dsss_receiver ===\n");
    printf("Settings: velocity=%.1f, txPower=%.1f, oversampling=%.1f (sps=%d)\n",
           settings.velocity, settings.txPower, settings.oversampling,
           (int)(2 * settings.oversampling));

    // Call MATLAB generated function
    dsss_receiver(otaBuffer, &settings, rxPayload, &errs, &rxStatus);

    printf("\n=== MATLAB Coder Results ===\n");
    printf("rxStatus: %s\n", rxStatus ? "SUCCESS" : "FAILED");
    printf("Errors: %d\n", errs);

    if (rxStatus) {
        printf("\nReceived payload (202 bits):\n");
        for (int i = 0; i < 202; i++) {
            printf("%d", rxPayload[i] ? 1 : 0);
            if ((i + 1) % 50 == 0) printf("\n");
        }
        printf("\n");
    }

    // Terminate
    dsss_receiver_terminate();

    return rxStatus ? 0 : 1;
}
