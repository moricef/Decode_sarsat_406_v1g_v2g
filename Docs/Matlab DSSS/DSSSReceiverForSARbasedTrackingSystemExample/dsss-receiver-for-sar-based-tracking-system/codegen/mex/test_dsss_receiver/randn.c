/*
 * randn.c
 *
 * Code generation for function 'randn'
 *
 */

/* Include files */
#include "randn.h"
#include "rt_nonfinite.h"

/* Function Definitions */
void randn(real_T r[307200])
{
  emlrtRandn(&r[0], 307200);
}

/* End of code generation (randn.c) */
