/*
 * File: pskdemod.c
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

/* Include Files */
#include "pskdemod.h"
#include "dsss_receiver_emxutil.h"
#include "dsss_receiver_rtwutil.h"
#include "dsss_receiver_types.h"
#include "rt_nonfinite.h"
#include <emmintrin.h>
#include <math.h>

/* Function Declarations */
static double rt_roundd_snf(double u);

/* Function Definitions */
/*
 * Arguments    : double u
 * Return Type  : double
 */
static double rt_roundd_snf(double u)
{
  double y;
  if (fabs(u) < 4.503599627370496E+15) {
    if (u >= 0.5) {
      y = floor(u + 0.5);
    } else if (u > -0.5) {
      y = u * 0.0;
    } else {
      y = ceil(u - 0.5);
    }
  } else {
    y = u;
  }
  return y;
}

/*
 * Arguments    : const emxArray_creal_T *y
 *                emxArray_real_T *z
 * Return Type  : void
 */
void pskdemod(const emxArray_creal_T *y, emxArray_real_T *z)
{
  static const signed char gray_map[4] = {0, 1, 3, 2};
  emxArray_creal_T *b_y1;
  emxArray_real_T *b_z1;
  emxArray_real_T *z1;
  const creal_T *y_data;
  creal_T *y1_data;
  double varargin_1;
  double *b_z1_data;
  double *z1_data;
  double *z_data;
  int i;
  int k;
  int loop_ub;
  int vectorUB;
  y_data = y->data;
  emxInit_creal_T(&b_y1);
  loop_ub = y->size[0];
  i = b_y1->size[0];
  b_y1->size[0] = y->size[0];
  emxEnsureCapacity_creal_T(b_y1, i);
  y1_data = b_y1->data;
  for (i = 0; i < loop_ub; i++) {
    double d;
    varargin_1 = y_data[i].re;
    d = y_data[i].im;
    y1_data[i].re = varargin_1 * 0.70710678118654757 - d * -0.70710678118654746;
    y1_data[i].im = varargin_1 * -0.70710678118654746 + d * 0.70710678118654757;
  }
  emxInit_real_T(&z1, 1);
  i = z1->size[0];
  z1->size[0] = y->size[0];
  emxEnsureCapacity_real_T(z1, i);
  z1_data = z1->data;
  for (k = 0; k < loop_ub; k++) {
    z1_data[k] = rt_atan2d_snf(y1_data[k].im, y1_data[k].re);
  }
  emxFree_creal_T(&b_y1);
  k = (z1->size[0] / 2) << 1;
  vectorUB = k - 2;
  for (i = 0; i <= vectorUB; i += 2) {
    __m128d r;
    r = _mm_loadu_pd(&z1_data[i]);
    _mm_storeu_pd(&z1_data[i], _mm_mul_pd(r, _mm_set1_pd(0.63661977236758138)));
  }
  for (i = k; i < loop_ub; i++) {
    z1_data[i] *= 0.63661977236758138;
  }
  vectorUB = z1->size[0];
  for (k = 0; k < vectorUB; k++) {
    z1_data[k] = rt_roundd_snf(z1_data[k]);
  }
  k = z1->size[0] - 1;
  for (loop_ub = 0; loop_ub <= k; loop_ub++) {
    if (z1_data[loop_ub] < 0.0) {
      z1_data[loop_ub] += 4.0;
    }
  }
  emxInit_real_T(&b_z1, 1);
  i = b_z1->size[0];
  b_z1->size[0] = z1->size[0];
  emxEnsureCapacity_real_T(b_z1, i);
  b_z1_data = b_z1->data;
  for (i = 0; i < vectorUB; i++) {
    b_z1_data[i] = gray_map[(int)(z1_data[i] + 1.0) - 1];
  }
  i = z->size[0];
  z->size[0] = (int)((unsigned int)b_z1->size[0] << 1);
  emxEnsureCapacity_real_T(z, i);
  z_data = z->data;
  for (k = 0; k < 2; k++) {
    i = z1->size[0];
    z1->size[0] = vectorUB;
    emxEnsureCapacity_real_T(z1, i);
    z1_data = z1->data;
    for (i = 0; i < vectorUB; i++) {
      varargin_1 = b_z1_data[i];
      z1_data[i] = (((long long)varargin_1 & 1LL << k) != 0LL);
    }
    for (loop_ub = 0; loop_ub < vectorUB; loop_ub++) {
      z_data[(((loop_ub + 1) << 1) - k) - 1] = z1_data[loop_ub];
    }
  }
  emxFree_real_T(&b_z1);
  emxFree_real_T(&z1);
}

/*
 * File trailer for pskdemod.c
 *
 * [EOF]
 */
