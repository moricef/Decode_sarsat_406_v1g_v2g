/*
 * helperPolyphaseCorrelator.h
 *
 * Code generation for function 'helperPolyphaseCorrelator'
 *
 */

#pragma once

/* Include files */
#include "rtwtypes.h"
#include "test_dsss_receiver_types.h"
#include "emlrt.h"
#include "mex.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Function Declarations */
void helperPolyphaseCorrelator(test_dsss_receiverStackData *SD,
                               const emlrtStack *sp,
                               const creal_T rxBuffer[307200],
                               const creal_T referenceSignal[175],
                               real_T idx_data[], int32_T idx_size[2],
                               real_T corrBuffer[38400]);

/* End of code generation (helperPolyphaseCorrelator.h) */
