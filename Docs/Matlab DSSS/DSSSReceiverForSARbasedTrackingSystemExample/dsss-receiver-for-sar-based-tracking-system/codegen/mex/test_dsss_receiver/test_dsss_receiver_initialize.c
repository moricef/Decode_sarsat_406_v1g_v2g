/*
 * test_dsss_receiver_initialize.c
 *
 * Code generation for function 'test_dsss_receiver_initialize'
 *
 */

/* Include files */
#include "test_dsss_receiver_initialize.h"
#include "_coder_test_dsss_receiver_mex.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_data.h"

/* Function Declarations */
static void test_dsss_receiver_once(void);

/* Function Definitions */
static void test_dsss_receiver_once(void)
{
  mex_InitInfAndNan();
}

void test_dsss_receiver_initialize(void)
{
  emlrtStack st = {
      NULL, /* site */
      NULL, /* tls */
      NULL  /* prev */
  };
  mexFunctionCreateRootTLS();
  st.tls = emlrtRootTLSGlobal;
  emlrtBreakCheckR2012bFlagVar = emlrtGetBreakCheckFlagAddressR2022b(&st);
  emlrtClearAllocCountR2012b(&st, false, 0U, NULL);
  emlrtEnterRtStackR2012b(&st);
  emlrtLicenseCheckR2022a(&st, "EMLRT:runTime:MexFunctionNeedsLicense",
                          "communication_toolbox", 2);
  if (emlrtFirstTimeR2012b(emlrtRootTLSGlobal)) {
    test_dsss_receiver_once();
  }
}

/* End of code generation (test_dsss_receiver_initialize.c) */
