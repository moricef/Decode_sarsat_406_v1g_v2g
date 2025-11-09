/*
 * sumMatrixIncludeNaN.c
 *
 * Code generation for function 'sumMatrixIncludeNaN'
 *
 */

/* Include files */
#include "sumMatrixIncludeNaN.h"
#include "rt_nonfinite.h"

/* Function Definitions */
real_T b_sumColumnB(const real_T x[256])
{
  real_T y;
  int32_T k;
  y = x[0];
  for (k = 0; k < 255; k++) {
    y += x[k + 1];
  }
  return y;
}

real_T b_sumColumnB4(const real_T x[38400], int32_T vstart)
{
  real_T psum1;
  real_T psum2;
  real_T psum3;
  real_T psum4;
  int32_T k;
  psum1 = x[vstart - 1];
  psum2 = x[vstart + 1023];
  psum3 = x[vstart + 2047];
  psum4 = x[vstart + 3071];
  for (k = 0; k < 1023; k++) {
    int32_T psum1_tmp;
    psum1_tmp = vstart + k;
    psum1 += x[psum1_tmp];
    psum2 += x[psum1_tmp + 1024];
    psum3 += x[psum1_tmp + 2048];
    psum4 += x[psum1_tmp + 3072];
  }
  return (psum1 + psum2) + (psum3 + psum4);
}

real_T sumColumnB(const real_T x[38400])
{
  real_T b_y;
  real_T y;
  int32_T k;
  y = x[36864];
  for (k = 0; k < 1023; k++) {
    y += x[k + 36865];
  }
  b_y = x[37888];
  for (k = 0; k < 511; k++) {
    b_y += x[k + 37889];
  }
  y += b_y;
  return y;
}

real_T sumColumnB4(const real_T x[38400])
{
  real_T psum1;
  real_T psum2;
  real_T psum3;
  real_T psum4;
  int32_T k;
  psum1 = x[0];
  psum2 = x[1024];
  psum3 = x[2048];
  psum4 = x[3072];
  for (k = 0; k < 1023; k++) {
    psum1 += x[k + 1];
    psum2 += x[k + 1025];
    psum3 += x[k + 2049];
    psum4 += x[k + 3073];
  }
  return (psum1 + psum2) + (psum3 + psum4);
}

/* End of code generation (sumMatrixIncludeNaN.c) */
