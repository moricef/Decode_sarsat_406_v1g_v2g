/*
 * int2bit.c
 *
 * Code generation for function 'int2bit'
 *
 */

/* Include files */
#include "int2bit.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_data.h"
#include "test_dsss_receiver_emxutil.h"
#include "test_dsss_receiver_types.h"
#include "mwmathutil.h"

/* Variable Definitions */
static emlrtRSInfo ob_emlrtRSI = {
    59,        /* lineNo */
    "int2bit", /* fcnName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\int2bit.m" /* pathName
                                                                         */
};

static emlrtRSInfo pb_emlrtRSI = {
    138,       /* lineNo */
    "int2bit", /* fcnName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\int2bit.m" /* pathName
                                                                         */
};

static emlrtRSInfo qb_emlrtRSI = {
    173,         /* lineNo */
    "int2bitCG", /* fcnName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\int2bit.m" /* pathName
                                                                         */
};

static emlrtRTEInfo d_emlrtRTEI = {
    64,       /* lineNo */
    9,        /* colNo */
    "bitget", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\lib\\matlab\\ops\\bitget.m" /* pName
                                                                       */
};

static emlrtRTEInfo e_emlrtRTEI = {
    13,                /* lineNo */
    37,                /* colNo */
    "validateinteger", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\eml\\+coder\\+internal\\+"
    "valattr\\validateinteger.m" /* pName */
};

static emlrtRTEInfo w_emlrtRTEI = {
    149,       /* lineNo */
    28,        /* colNo */
    "int2bit", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\int2bit.m" /* pName
                                                                         */
};

static emlrtRTEInfo x_emlrtRTEI = {
    173,       /* lineNo */
    13,        /* colNo */
    "int2bit", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\int2bit.m" /* pName
                                                                         */
};

static emlrtRTEInfo y_emlrtRTEI = {
    156,       /* lineNo */
    13,        /* colNo */
    "int2bit", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\int2bit.m" /* pName
                                                                         */
};

/* Function Definitions */
void int2bit(const emlrtStack *sp, const emxArray_real_T *x, emxArray_real_T *y)
{
  emlrtStack b_st;
  emlrtStack st;
  emxArray_real_T *tmp;
  const real_T *x_data;
  real_T *tmp_data;
  real_T *y_data;
  int32_T b_k;
  int32_T k;
  int32_T loop_ub_tmp;
  boolean_T exitg1;
  boolean_T p;
  st.prev = sp;
  st.tls = sp->tls;
  b_st.prev = &st;
  b_st.tls = st.tls;
  x_data = x->data;
  emlrtHeapReferenceStackEnterFcnR2012b((emlrtConstCTX)sp);
  st.site = &ob_emlrtRSI;
  b_st.site = &w_emlrtRSI;
  p = true;
  k = 0;
  exitg1 = false;
  while ((!exitg1) && (k <= x->size[0] - 1)) {
    if ((!muDoubleScalarIsInf(x_data[k])) &&
        (!muDoubleScalarIsNaN(x_data[k])) &&
        (muDoubleScalarFloor(x_data[k]) == x_data[k])) {
      k++;
    } else {
      p = false;
      exitg1 = true;
    }
  }
  if (!p) {
    emlrtErrorWithMessageIdR2018a(
        &b_st, &e_emlrtRTEI, "Coder:toolbox:ValidateattributesexpectedInteger",
        "MATLAB:expectedInteger", 3, 4, 1, "X");
  }
  st.site = &pb_emlrtRSI;
  b_k = y->size[0];
  y->size[0] = x->size[0] << 1;
  emxEnsureCapacity_real_T(&st, y, b_k, &w_emlrtRTEI);
  y_data = y->data;
  loop_ub_tmp = x->size[0];
  emxInit_real_T(&st, &tmp, &y_emlrtRTEI);
  for (k = 0; k < 2; k++) {
    b_st.site = &qb_emlrtRSI;
    p = true;
    b_k = 0;
    exitg1 = false;
    while ((!exitg1) && (b_k <= x->size[0] - 1)) {
      if (!(x_data[b_k] == muDoubleScalarFloor(x_data[b_k]))) {
        p = false;
        exitg1 = true;
      } else {
        b_k++;
      }
    }
    if (!p) {
      emlrtErrorWithMessageIdR2018a(&b_st, &d_emlrtRTEI,
                                    "MATLAB:bitget:outOfRange",
                                    "MATLAB:bitget:outOfRange", 0);
    }
    b_k = tmp->size[0];
    tmp->size[0] = loop_ub_tmp;
    emxEnsureCapacity_real_T(&b_st, tmp, b_k, &x_emlrtRTEI);
    tmp_data = tmp->data;
    for (b_k = 0; b_k < loop_ub_tmp; b_k++) {
      real_T varargin_1;
      varargin_1 = x_data[b_k];
      tmp_data[b_k] = (((int64_T)varargin_1 & 1LL << k) != 0LL);
    }
    for (b_k = 0; b_k < loop_ub_tmp; b_k++) {
      y_data[(((b_k + 1) << 1) - k) - 1] = tmp_data[b_k];
    }
  }
  emxFree_real_T(&st, &tmp);
  emlrtHeapReferenceStackLeaveFcnR2012b((emlrtConstCTX)sp);
}

/* End of code generation (int2bit.c) */
