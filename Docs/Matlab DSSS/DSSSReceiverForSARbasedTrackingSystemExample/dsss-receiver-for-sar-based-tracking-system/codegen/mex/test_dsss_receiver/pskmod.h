/*
 * pskmod.h
 *
 * Code generation for function 'pskmod'
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
void pskmod(test_dsss_receiverStackData *SD, const emlrtStack *sp,
            const boolean_T x[6400], creal_T y[3200]);

/* End of code generation (pskmod.h) */
