/*
 * _coder_test_dsss_receiver_mex.h
 *
 * Code generation for function '_coder_test_dsss_receiver_mex'
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
MEXFUNCTION_LINKAGE void mexFunction(int32_T nlhs, mxArray *plhs[],
                                     int32_T nrhs, const mxArray *prhs[]);

emlrtCTX mexFunctionCreateRootTLS(void);

void test_dsss_receiver_mexFunction(test_dsss_receiverStackData *SD,
                                    int32_T nlhs, int32_T nrhs);

/* End of code generation (_coder_test_dsss_receiver_mex.h) */
