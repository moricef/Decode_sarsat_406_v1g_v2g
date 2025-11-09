/*
 * File: helperPolyphaseCorrelator.h
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

#ifndef HELPERPOLYPHASECORRELATOR_H
#define HELPERPOLYPHASECORRELATOR_H

/* Include Files */
#include "dsss_receiver_types.h"
#include "rtwtypes.h"
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function Declarations */
void helperPolyphaseCorrelator(const emxArray_creal_T *rxBuffer,
                               const creal_T referenceSignal[175], double sps,
                               double idx_data[], int idx_size[2],
                               emxArray_real_T *corrBuffer);

#ifdef __cplusplus
}
#endif

#endif
/*
 * File trailer for helperPolyphaseCorrelator.h
 *
 * [EOF]
 */
