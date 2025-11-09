/*
 * conv.c
 *
 * Code generation for function 'conv'
 *
 */

/* Include files */
#include "conv.h"
#include "rt_nonfinite.h"
#include "blas.h"
#include "mwmathutil.h"
#include <stddef.h>
#include <string.h>

/* Function Definitions */
void conv(const creal_T A[38400], const creal_T B[175], creal_T C[38574])
{
  ptrdiff_t incx_t;
  ptrdiff_t incy_t;
  ptrdiff_t n_t;
  real_T B_re_tmp;
  real_T b_B_re_tmp;
  int32_T b_k;
  int32_T k;
  boolean_T guard1;
  boolean_T p;
  p = true;
  for (k = 0; k < 38400; k++) {
    if (p) {
      B_re_tmp = A[k].re;
      b_B_re_tmp = A[k].im;
      if (muDoubleScalarIsInf(B_re_tmp) || muDoubleScalarIsInf(b_B_re_tmp) ||
          (muDoubleScalarIsNaN(B_re_tmp) || muDoubleScalarIsNaN(b_B_re_tmp))) {
        p = false;
      }
    } else {
      p = false;
    }
  }
  guard1 = false;
  if (!p) {
    guard1 = true;
  } else {
    p = true;
    for (k = 0; k < 175; k++) {
      if (p) {
        B_re_tmp = B[k].re;
        b_B_re_tmp = B[k].im;
        if (muDoubleScalarIsInf(B_re_tmp) || muDoubleScalarIsInf(b_B_re_tmp) ||
            (muDoubleScalarIsNaN(B_re_tmp) ||
             muDoubleScalarIsNaN(b_B_re_tmp))) {
          p = false;
        }
      } else {
        p = false;
      }
    }
    if (!p) {
      guard1 = true;
    } else {
      memset(&C[0], 0, 38574U * sizeof(creal_T));
      n_t = (ptrdiff_t)38400;
      incx_t = (ptrdiff_t)1;
      incy_t = (ptrdiff_t)1;
      for (k = 0; k < 175; k++) {
        zaxpy(&n_t, (real_T *)&B[k], (real_T *)&A[0], &incx_t, (real_T *)&C[k],
              &incy_t);
      }
    }
  }
  if (guard1) {
    memset(&C[0], 0, 38574U * sizeof(creal_T));
    for (k = 0; k < 175; k++) {
      B_re_tmp = B[k].re;
      b_B_re_tmp = B[k].im;
      for (b_k = 0; b_k < 38400; b_k++) {
        real_T c_B_re_tmp;
        real_T d_B_re_tmp;
        int32_T i;
        c_B_re_tmp = A[b_k].im;
        d_B_re_tmp = A[b_k].re;
        i = k + b_k;
        C[i].re += B_re_tmp * d_B_re_tmp - b_B_re_tmp * c_B_re_tmp;
        C[i].im += B_re_tmp * c_B_re_tmp + b_B_re_tmp * d_B_re_tmp;
      }
    }
  }
}

/* End of code generation (conv.c) */
