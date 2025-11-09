/*
 * pskmod.c
 *
 * Code generation for function 'pskmod'
 *
 */

/* Include files */
#include "pskmod.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_types.h"
#include "blas.h"
#include "mwmathutil.h"
#include <emmintrin.h>
#include <stddef.h>

/* Variable Definitions */
static emlrtRSInfo t_emlrtRSI = {
    102,      /* lineNo */
    "pskmod", /* fcnName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskmod.m" /* pathName
                                                                        */
};

static emlrtBCInfo db_emlrtBCI = {
    1,        /* iFirst */
    4,        /* iLast */
    131,      /* lineNo */
    26,       /* colNo */
    "",       /* aName */
    "pskmod", /* fName */
    "C:\\Program Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\pskmod.m", /* pName
                                                                         */
    0 /* checkKind */
};

static emlrtDCInfo emlrtDCI = {
    38,         /* lineNo */
    34,         /* colNo */
    "bin2gray", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\+comm\\+internal\\+"
    "utilities\\bin2gray.m", /* pName */
    1                        /* checkKind */
};

static emlrtBCInfo eb_emlrtBCI = {
    1,          /* iFirst */
    4,          /* iLast */
    38,         /* lineNo */
    34,         /* colNo */
    "",         /* aName */
    "bin2gray", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\comm\\comm\\+comm\\+internal\\+"
    "utilities\\bin2gray.m", /* pName */
    0                        /* checkKind */
};

/* Function Definitions */
void pskmod(test_dsss_receiverStackData *SD, const emlrtStack *sp,
            const boolean_T x[6400], creal_T y[3200])
{
  static const creal_T b_const[4] = {{
                                         0.70710678118654757, /* re */
                                         0.70710678118654746  /* im */
                                     },
                                     {
                                         -0.70710678118654746, /* re */
                                         0.70710678118654757   /* im */
                                     },
                                     {
                                         -0.70710678118654768, /* re */
                                         -0.70710678118654746  /* im */
                                     },
                                     {
                                         0.70710678118654735, /* re */
                                         -0.70710678118654768 /* im */
                                     }};
  static const int8_T mapping[4] = {0, 1, 3, 2};
  ptrdiff_t k_t;
  ptrdiff_t lda_t;
  ptrdiff_t ldb_t;
  ptrdiff_t ldc_t;
  ptrdiff_t m_t;
  ptrdiff_t n_t;
  emlrtStack st;
  real_T dv[3200];
  real_T a[2];
  real_T dv1[2];
  real_T alpha1;
  real_T beta1;
  int32_T i;
  int32_T j;
  char_T TRANSA1;
  char_T TRANSB1;
  int8_T iloc[3200];
  st.prev = sp;
  st.tls = sp->tls;
  for (i = 0; i < 6400; i++) {
    SD->u1.f1.xMat[i] = x[i];
  }
  a[0] = 2.0;
  a[1] = 1.0;
  TRANSB1 = 'N';
  TRANSA1 = 'N';
  alpha1 = 1.0;
  beta1 = 0.0;
  m_t = (ptrdiff_t)1;
  n_t = (ptrdiff_t)3200;
  k_t = (ptrdiff_t)2;
  lda_t = (ptrdiff_t)1;
  ldb_t = (ptrdiff_t)2;
  ldc_t = (ptrdiff_t)1;
  dgemm(&TRANSA1, &TRANSB1, &m_t, &n_t, &k_t, &alpha1, &a[0], &lda_t,
        &SD->u1.f1.xMat[0], &ldb_t, &beta1, &dv[0], &ldc_t);
  st.site = &t_emlrtRSI;
  for (i = 0; i <= 3198; i += 2) {
    __m128d r;
    r = _mm_loadu_pd(&dv[i]);
    _mm_storeu_pd(&dv1[0], _mm_add_pd(r, _mm_set1_pd(1.0)));
    if (dv1[0] != (int32_T)muDoubleScalarFloor(dv1[0])) {
      emlrtIntegerCheckR2012b(dv1[0], &emlrtDCI, &st);
    }
    if (dv1[1] != (int32_T)muDoubleScalarFloor(dv1[1])) {
      emlrtIntegerCheckR2012b(dv1[1], &emlrtDCI, &st);
    }
    if (((int32_T)dv1[0] < 1) || ((int32_T)dv1[0] > 4)) {
      emlrtDynamicBoundsCheckR2012b((int32_T)dv1[0], 1, 4, &eb_emlrtBCI, &st);
    }
    if (((int32_T)dv1[1] < 1) || ((int32_T)dv1[1] > 4)) {
      emlrtDynamicBoundsCheckR2012b((int32_T)dv1[1], 1, 4, &eb_emlrtBCI, &st);
    }
  }
  for (j = 0; j < 3200; j++) {
    boolean_T exitg1;
    iloc[j] = 0;
    i = 0;
    exitg1 = false;
    while ((!exitg1) && (i < 4)) {
      if (dv[j] == mapping[i]) {
        iloc[j] = (int8_T)(i + 1);
        exitg1 = true;
      } else {
        i++;
      }
    }
  }
  for (i = 0; i < 3200; i++) {
    int8_T b_i;
    b_i = iloc[i];
    if ((b_i < 1) || (b_i > 4)) {
      emlrtDynamicBoundsCheckR2012b(b_i, 1, 4, &db_emlrtBCI, (emlrtConstCTX)sp);
    }
    y[i].re = b_const[b_i - 1].re;
    y[i].im = b_const[b_i - 1].im;
  }
}

/* End of code generation (pskmod.c) */
