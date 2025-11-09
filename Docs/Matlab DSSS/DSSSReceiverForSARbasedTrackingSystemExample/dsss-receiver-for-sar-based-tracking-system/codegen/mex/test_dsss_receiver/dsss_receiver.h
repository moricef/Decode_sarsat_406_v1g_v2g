/*
 * dsss_receiver.h
 *
 * Code generation for function 'dsss_receiver'
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
int32_T dsss_receiver(test_dsss_receiverStackData *SD, const emlrtStack *sp,
                      const creal_T otaBuffer[307200], boolean_T rxPayload[202],
                      boolean_T *rxStatus);

/* End of code generation (dsss_receiver.h) */
