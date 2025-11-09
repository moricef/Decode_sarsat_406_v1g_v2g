/*
 * File: dsss_receiver.h
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

#ifndef DSSS_RECEIVER_H
#define DSSS_RECEIVER_H

/* Include Files */
#include "dsss_receiver_types.h"
#include "rtwtypes.h"
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function Declarations */
extern void dsss_receiver(const creal_T otaBuffer[307200],
                          const struct0_T *settings, boolean_T rxPayload[202],
                          int *errs, boolean_T *rxStatus);

#ifdef __cplusplus
}
#endif

#endif
/*
 * File trailer for dsss_receiver.h
 *
 * [EOF]
 */
