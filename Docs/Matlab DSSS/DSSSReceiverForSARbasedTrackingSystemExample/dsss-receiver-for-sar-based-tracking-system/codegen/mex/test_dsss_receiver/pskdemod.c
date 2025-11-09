/*
 * pskdemod.c
 *
 * Code generation for function 'pskdemod'
 *
 */

/* Include files */
#include "pskdemod.h"
#include "int2bit.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_data.h"
#include "test_dsss_receiver_emxutil.h"
#include "test_dsss_receiver_types.h"
#include "mwmathutil.h"
#include <emmintrin.h>

/* Variable Definitions */
static emlrtRSInfo mb_emlrtRSI = {
    135,        /* lineNo */
    "pskdemod", /* fcnName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pathName
                                                                          */
};

static emlrtRSInfo nb_emlrtRSI = {
    166,        /* lineNo */
    "pskdemod", /* fcnName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pathName
                                                                          */
};

static emlrtRTEInfo c_emlrtRTEI = {
    14,               /* lineNo */
    37,               /* colNo */
    "validatefinite", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\eml\\+coder\\+internal\\+"
    "valattr\\validatefinite.m" /* pName */
};

static emlrtECInfo d_emlrtECI = {
    -1,         /* nDims */
    157,        /* lineNo */
    13,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

static emlrtBCInfo ib_emlrtBCI = {
    -1,         /* iFirst */
    -1,         /* iLast */
    151,        /* lineNo */
    26,         /* colNo */
    "",         /* aName */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m", /* pName
                                                                           */
    0 /* checkKind */
};

static emlrtBCInfo jb_emlrtBCI = {
    -1,         /* iFirst */
    -1,         /* iLast */
    151,        /* lineNo */
    9,          /* colNo */
    "",         /* aName */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m", /* pName
                                                                           */
    0 /* checkKind */
};

static emlrtBCInfo kb_emlrtBCI = {
    1,          /* iFirst */
    4,          /* iLast */
    157,        /* lineNo */
    30,         /* colNo */
    "",         /* aName */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m", /* pName
                                                                           */
    0 /* checkKind */
};

static emlrtDCInfo b_emlrtDCI = {
    157,        /* lineNo */
    30,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m", /* pName
                                                                           */
    1 /* checkKind */
};

static emlrtRTEInfo p_emlrtRTEI = {
    142,        /* lineNo */
    14,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

static emlrtRTEInfo q_emlrtRTEI = {
    30,                    /* lineNo */
    21,                    /* colNo */
    "applyScalarFunction", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\eml\\+coder\\+"
    "internal\\applyScalarFunction.m" /* pName */
};

static emlrtRTEInfo r_emlrtRTEI = {
    1,          /* lineNo */
    14,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

static emlrtRTEInfo s_emlrtRTEI = {
    151,        /* lineNo */
    19,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

static emlrtRTEInfo t_emlrtRTEI = {
    157,        /* lineNo */
    21,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

static emlrtRTEInfo u_emlrtRTEI = {
    148,        /* lineNo */
    9,          /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

static emlrtRTEInfo v_emlrtRTEI = {
    151,        /* lineNo */
    12,         /* colNo */
    "pskdemod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskdemod.m" /* pName
                                                                          */
};

/* Function Definitions */
void pskdemod(const emlrtStack *sp, const emxArray_creal_T *y,
              emxArray_real_T *z)
{
  static const int8_T gray_map[4] = {0, 1, 3, 2};
  emlrtStack b_st;
  emlrtStack st;
  emxArray_creal_T *x;
  emxArray_int32_T *r1;
  emxArray_real_T *b_z1;
  emxArray_real_T *z1;
  const creal_T *y_data;
  creal_T *x_data;
  real_T d;
  real_T *b_z1_data;
  real_T *z1_data;
  int32_T i;
  int32_T k;
  int32_T loop_ub;
  int32_T nx;
  int32_T *r2;
  boolean_T exitg1;
  boolean_T p;
  st.prev = sp;
  st.tls = sp->tls;
  b_st.prev = &st;
  b_st.tls = st.tls;
  y_data = y->data;
  emlrtHeapReferenceStackEnterFcnR2012b((emlrtConstCTX)sp);
  st.site = &mb_emlrtRSI;
  b_st.site = &w_emlrtRSI;
  p = true;
  k = 0;
  exitg1 = false;
  while ((!exitg1) && (k <= y->size[0] - 1)) {
    if ((!muDoubleScalarIsInf(y_data[k].re)) &&
        (!muDoubleScalarIsInf(y_data[k].im)) &&
        ((!muDoubleScalarIsNaN(y_data[k].re)) &&
         (!muDoubleScalarIsNaN(y_data[k].im)))) {
      k++;
    } else {
      p = false;
      exitg1 = true;
    }
  }
  if (!p) {
    emlrtErrorWithMessageIdR2018a(
        &b_st, &c_emlrtRTEI, "Coder:toolbox:ValidateattributesexpectedFinite",
        "MATLAB:pskdemod:expectedFinite", 3, 4, 1, "Y");
  }
  emxInit_creal_T(sp, &x, &p_emlrtRTEI);
  loop_ub = y->size[0];
  i = x->size[0];
  x->size[0] = y->size[0];
  emxEnsureCapacity_creal_T(sp, x, i, &p_emlrtRTEI);
  x_data = x->data;
  for (i = 0; i < loop_ub; i++) {
    real_T d1;
    d = y_data[i].re;
    d1 = y_data[i].im;
    x_data[i].re = d * 0.70710678118654757 - d1 * -0.70710678118654746;
    x_data[i].im = d * -0.70710678118654746 + d1 * 0.70710678118654757;
  }
  emxInit_real_T(sp, &z1, &u_emlrtRTEI);
  i = z1->size[0];
  z1->size[0] = y->size[0];
  emxEnsureCapacity_real_T(sp, z1, i, &q_emlrtRTEI);
  z1_data = z1->data;
  for (k = 0; k < loop_ub; k++) {
    z1_data[k] = muDoubleScalarAtan2(x_data[k].im, x_data[k].re);
  }
  emxFree_creal_T(sp, &x);
  nx = (z1->size[0] / 2) << 1;
  k = nx - 2;
  for (i = 0; i <= k; i += 2) {
    __m128d r;
    r = _mm_loadu_pd(&z1_data[i]);
    _mm_storeu_pd(&z1_data[i], _mm_mul_pd(r, _mm_set1_pd(0.63661977236758138)));
  }
  for (i = nx; i < loop_ub; i++) {
    z1_data[i] *= 0.63661977236758138;
  }
  nx = z1->size[0];
  for (k = 0; k < nx; k++) {
    z1_data[k] = muDoubleScalarRound(z1_data[k]);
  }
  k = z1->size[0] - 1;
  nx = 0;
  for (loop_ub = 0; loop_ub <= k; loop_ub++) {
    if (z1_data[loop_ub] < 0.0) {
      nx++;
    }
  }
  emxInit_int32_T(sp, &r1, &v_emlrtRTEI);
  i = r1->size[0];
  r1->size[0] = nx;
  emxEnsureCapacity_int32_T(sp, r1, i, &r_emlrtRTEI);
  r2 = r1->data;
  nx = 0;
  for (loop_ub = 0; loop_ub <= k; loop_ub++) {
    if (z1_data[loop_ub] < 0.0) {
      r2[nx] = loop_ub;
      nx++;
    }
  }
  nx = r1->size[0];
  for (i = 0; i < nx; i++) {
    if (r2[i] > k) {
      emlrtDynamicBoundsCheckR2012b(r2[i], 0, k, &ib_emlrtBCI,
                                    (emlrtConstCTX)sp);
    }
  }
  emxInit_real_T(sp, &b_z1, &s_emlrtRTEI);
  i = b_z1->size[0];
  b_z1->size[0] = r1->size[0];
  emxEnsureCapacity_real_T(sp, b_z1, i, &s_emlrtRTEI);
  b_z1_data = b_z1->data;
  for (i = 0; i < nx; i++) {
    b_z1_data[i] = z1_data[r2[i]] + 4.0;
  }
  for (i = 0; i < nx; i++) {
    if (r2[i] > z1->size[0] - 1) {
      emlrtDynamicBoundsCheckR2012b(r2[i], 0, z1->size[0] - 1, &jb_emlrtBCI,
                                    (emlrtConstCTX)sp);
    }
    z1_data[r2[i]] = b_z1_data[i];
  }
  emxFree_int32_T(sp, &r1);
  loop_ub = z1->size[0];
  i = b_z1->size[0];
  b_z1->size[0] = z1->size[0];
  emxEnsureCapacity_real_T(sp, b_z1, i, &t_emlrtRTEI);
  b_z1_data = b_z1->data;
  for (i = 0; i < loop_ub; i++) {
    d = z1_data[i] + 1.0;
    if (d != (int32_T)muDoubleScalarFloor(d)) {
      emlrtIntegerCheckR2012b(d, &b_emlrtDCI, (emlrtConstCTX)sp);
    }
    if (((int32_T)d < 1) || ((int32_T)d > 4)) {
      emlrtDynamicBoundsCheckR2012b((int32_T)d, 1, 4, &kb_emlrtBCI,
                                    (emlrtConstCTX)sp);
    }
    b_z1_data[i] = gray_map[(int32_T)d - 1];
  }
  if (z1->size[0] != b_z1->size[0]) {
    emlrtSubAssignSizeCheck1dR2017a(z1->size[0], b_z1->size[0], &d_emlrtECI,
                                    (emlrtConstCTX)sp);
  }
  emxFree_real_T(sp, &z1);
  st.site = &nb_emlrtRSI;
  int2bit(&st, b_z1, z);
  emxFree_real_T(sp, &b_z1);
  emlrtHeapReferenceStackLeaveFcnR2012b((emlrtConstCTX)sp);
}

/* End of code generation (pskdemod.c) */
