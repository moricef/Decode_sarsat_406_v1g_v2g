/*
 * test_dsss_receiver.c
 *
 * Code generation for function 'test_dsss_receiver'
 *
 */

/* Include files */
#include "test_dsss_receiver.h"
#include "dsss_receiver.h"
#include "randn.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_types.h"
#include <stdio.h>

/* Variable Definitions */
static emlrtRSInfo emlrtRSI = {
    16,                   /* lineNo */
    "test_dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\test_dsss_receiver.m" /* pathName */
};

static emlrtRSInfo b_emlrtRSI = {
    19,                   /* lineNo */
    "test_dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\test_dsss_receiver.m" /* pathName */
};

static emlrtRSInfo c_emlrtRSI = {
    23,                   /* lineNo */
    "test_dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\test_dsss_receiver.m" /* pathName */
};

static emlrtRSInfo d_emlrtRSI = {
    25,                   /* lineNo */
    "test_dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\test_dsss_receiver.m" /* pathName */
};

static emlrtRSInfo dc_emlrtRSI = {
    38,        /* lineNo */
    "fprintf", /* fcnName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\lib\\matlab\\iofun\\fprintf.m" /* pathName
                                                                          */
};

static emlrtRSInfo ec_emlrtRSI = {
    35,        /* lineNo */
    "fprintf", /* fcnName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\lib\\matlab\\iofun\\fprintf.m" /* pathName
                                                                          */
};

static emlrtMCInfo emlrtMCI = {
    66,        /* lineNo */
    18,        /* colNo */
    "fprintf", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\lib\\matlab\\iofun\\fprintf.m" /* pName
                                                                          */
};

static emlrtRSInfo hc_emlrtRSI = {
    66,        /* lineNo */
    "fprintf", /* fcnName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\lib\\matlab\\iofun\\fprintf.m" /* pathName
                                                                          */
};

/* Function Declarations */
static real_T b_emlrt_marshallIn(const emlrtStack *sp, const mxArray *u,
                                 const emlrtMsgIdentifier *parentId);

static const mxArray *b_feval(const emlrtStack *sp, const mxArray *m1,
                              const mxArray *m2, const mxArray *m3,
                              emlrtMCInfo *location);

static real_T c_emlrt_marshallIn(const emlrtStack *sp, const mxArray *src,
                                 const emlrtMsgIdentifier *msgId);

static real_T emlrt_marshallIn(const emlrtStack *sp,
                               const mxArray *a__output_of_feval_,
                               const char_T *identifier);

static const mxArray *feval(const emlrtStack *sp, const mxArray *m1,
                            const mxArray *m2, const mxArray *m3,
                            const mxArray *m4, emlrtMCInfo *location);

/* Function Definitions */
static real_T b_emlrt_marshallIn(const emlrtStack *sp, const mxArray *u,
                                 const emlrtMsgIdentifier *parentId)
{
  real_T y;
  y = c_emlrt_marshallIn(sp, emlrtAlias(u), parentId);
  emlrtDestroyArray(&u);
  return y;
}

static const mxArray *b_feval(const emlrtStack *sp, const mxArray *m1,
                              const mxArray *m2, const mxArray *m3,
                              emlrtMCInfo *location)
{
  const mxArray *pArrays[3];
  const mxArray *m;
  pArrays[0] = m1;
  pArrays[1] = m2;
  pArrays[2] = m3;
  return emlrtCallMATLABR2012b((emlrtConstCTX)sp, 1, &m, 3, &pArrays[0],
                               "feval", true, location);
}

static real_T c_emlrt_marshallIn(const emlrtStack *sp, const mxArray *src,
                                 const emlrtMsgIdentifier *msgId)
{
  static const int32_T dims = 0;
  real_T ret;
  emlrtCheckBuiltInR2012b((emlrtConstCTX)sp, msgId, src, "double", false, 0U,
                          (const void *)&dims);
  ret = *(real_T *)emlrtMxGetData(src);
  emlrtDestroyArray(&src);
  return ret;
}

static real_T emlrt_marshallIn(const emlrtStack *sp,
                               const mxArray *a__output_of_feval_,
                               const char_T *identifier)
{
  emlrtMsgIdentifier thisId;
  real_T y;
  thisId.fIdentifier = (const char_T *)identifier;
  thisId.fParent = NULL;
  thisId.bParentIsCell = false;
  y = b_emlrt_marshallIn(sp, emlrtAlias(a__output_of_feval_), &thisId);
  emlrtDestroyArray(&a__output_of_feval_);
  return y;
}

static const mxArray *feval(const emlrtStack *sp, const mxArray *m1,
                            const mxArray *m2, const mxArray *m3,
                            const mxArray *m4, emlrtMCInfo *location)
{
  const mxArray *pArrays[4];
  const mxArray *m;
  pArrays[0] = m1;
  pArrays[1] = m2;
  pArrays[2] = m3;
  pArrays[3] = m4;
  return emlrtCallMATLABR2012b((emlrtConstCTX)sp, 1, &m, 4, &pArrays[0],
                               "feval", true, location);
}

void test_dsss_receiver(test_dsss_receiverStackData *SD, const emlrtStack *sp)
{
  static const int32_T iv[2] = {1, 7};
  static const int32_T iv1[2] = {1, 7};
  static const int32_T iv2[2] = {1, 23};
  static const int32_T iv3[2] = {1, 43};
  static const char_T c_u[43] = {
      'D',    '\xe9', 't', 'e', 'c', 't', 'i', 'o', 'n',  ' ', 'r',
      '\xe9', 'u',    's', 's', 'i', 'e', ',', ' ', 'e',  'r', 'r',
      'e',    'u',    'r', 's', ' ', 'c', 'o', 'r', 'r',  'i', 'g',
      '\xe9', 'e',    's', ' ', ':', ' ', '%', 'd', '\\', 'n'};
  static const char_T b_u[23] = {'\xc9', 'c', 'h', 'e', 'c', ' ',    'd', 'e',
                                 ' ',    'l', 'a', ' ', 'd', '\xe9', 't', 'e',
                                 'c',    't', 'i', 'o', 'n', '\\',   'n'};
  static const char_T u[7] = {'f', 'p', 'r', 'i', 'n', 't', 'f'};
  emlrtStack b_st;
  emlrtStack c_st;
  emlrtStack st;
  const mxArray *b_y;
  const mxArray *c_y;
  const mxArray *d_y;
  const mxArray *e_y;
  const mxArray *f_y;
  const mxArray *g_y;
  const mxArray *m;
  const mxArray *y;
  int32_T i;
  boolean_T rxStatus;
  st.prev = sp;
  st.tls = sp->tls;
  b_st.prev = &st;
  b_st.tls = st.tls;
  c_st.prev = &b_st;
  c_st.tls = b_st.tls;
  /*  Test du récepteur DSSS */
  /*  Paramètres */
  /*  Calcul de la taille du buffer */
  /*  Génère un signal d'entrée de la bonne taille */
  st.site = &emlrtRSI;
  randn(SD->f3.dv);
  st.site = &emlrtRSI;
  randn(SD->f3.dv1);
  for (i = 0; i < 307200; i++) {
    SD->f3.otaBuffer[i].re = SD->f3.dv[i];
    SD->f3.otaBuffer[i].im = SD->f3.dv1[i];
  }
  boolean_T rxPayload[202];
  /*  Appelle la fonction */
  st.site = &b_emlrtRSI;
  dsss_receiver(SD, &st, SD->f3.otaBuffer, rxPayload, &rxStatus);
  /*  Affichage compatible codegen */
  if (rxStatus) {
    st.site = &c_emlrtRSI;
    b_st.site = &dc_emlrtRSI;
    b_y = NULL;
    m = emlrtCreateCharArray(2, &iv1[0]);
    emlrtInitCharArrayR2013a(&b_st, 7, m, &u[0]);
    emlrtAssign(&b_y, m);
    d_y = NULL;
    m = emlrtCreateDoubleScalar(1.0);
    emlrtAssign(&d_y, m);
    f_y = NULL;
    m = emlrtCreateCharArray(2, &iv3[0]);
    emlrtInitCharArrayR2013a(&b_st, 43, m, &c_u[0]);
    emlrtAssign(&f_y, m);
    g_y = NULL;
    m = emlrtCreateNumericMatrix(1, 1, mxINT32_CLASS, mxREAL);
    *(int32_T *)emlrtMxGetData(m) = 0;
    emlrtAssign(&g_y, m);
    c_st.site = &hc_emlrtRSI;
    emlrt_marshallIn(&c_st, feval(&c_st, b_y, d_y, f_y, g_y, &emlrtMCI),
                     "<output of feval>");
  } else {
    st.site = &d_emlrtRSI;
    b_st.site = &ec_emlrtRSI;
    y = NULL;
    m = emlrtCreateCharArray(2, &iv[0]);
    emlrtInitCharArrayR2013a(&b_st, 7, m, &u[0]);
    emlrtAssign(&y, m);
    c_y = NULL;
    m = emlrtCreateDoubleScalar(1.0);
    emlrtAssign(&c_y, m);
    e_y = NULL;
    m = emlrtCreateCharArray(2, &iv2[0]);
    emlrtInitCharArrayR2013a(&b_st, 23, m, &b_u[0]);
    emlrtAssign(&e_y, m);
    c_st.site = &hc_emlrtRSI;
    emlrt_marshallIn(&c_st, b_feval(&c_st, y, c_y, e_y, &emlrtMCI),
                     "<output of feval>");
  }
}

/* End of code generation (test_dsss_receiver.c) */
