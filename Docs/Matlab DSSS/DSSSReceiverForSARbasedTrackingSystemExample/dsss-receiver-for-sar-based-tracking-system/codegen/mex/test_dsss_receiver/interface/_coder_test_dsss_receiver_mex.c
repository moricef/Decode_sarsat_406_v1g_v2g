/*
 * _coder_test_dsss_receiver_mex.c
 *
 * Code generation for function '_coder_test_dsss_receiver_mex'
 *
 */

/* Include files */
#include "_coder_test_dsss_receiver_mex.h"
#include "_coder_test_dsss_receiver_api.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_data.h"
#include "test_dsss_receiver_initialize.h"
#include "test_dsss_receiver_terminate.h"
#include "test_dsss_receiver_types.h"

/* Function Definitions */
void mexFunction(int32_T nlhs, mxArray *plhs[], int32_T nrhs,
                 const mxArray *prhs[])
{
  test_dsss_receiverStackData *c_test_dsss_receiverStackDataGl = NULL;
  (void)plhs;
  (void)prhs;
  c_test_dsss_receiverStackDataGl =
      (test_dsss_receiverStackData *)emlrtMxCalloc(
          (size_t)1, (size_t)1U * sizeof(test_dsss_receiverStackData));
  mexAtExit(&test_dsss_receiver_atexit);
  /* Module initialization. */
  test_dsss_receiver_initialize();
  /* Dispatch the entry-point. */
  test_dsss_receiver_mexFunction(c_test_dsss_receiverStackDataGl, nlhs, nrhs);
  /* Module termination. */
  test_dsss_receiver_terminate();
  emlrtMxFree(c_test_dsss_receiverStackDataGl);
}

emlrtCTX mexFunctionCreateRootTLS(void)
{
  emlrtCreateRootTLSR2022a(&emlrtRootTLSGlobal, &emlrtContextGlobal, NULL, 1,
                           NULL, "windows-1252", true);
  return emlrtRootTLSGlobal;
}

void test_dsss_receiver_mexFunction(test_dsss_receiverStackData *SD,
                                    int32_T nlhs, int32_T nrhs)
{
  emlrtStack st = {
      NULL, /* site */
      NULL, /* tls */
      NULL  /* prev */
  };
  st.tls = emlrtRootTLSGlobal;
  /* Check for proper number of arguments. */
  if (nrhs != 0) {
    emlrtErrMsgIdAndTxt(&st, "EMLRT:runTime:WrongNumberOfInputs", 5, 12, 0, 4,
                        18, "test_dsss_receiver");
  }
  if (nlhs > 0) {
    emlrtErrMsgIdAndTxt(&st, "EMLRT:runTime:TooManyOutputArguments", 3, 4, 18,
                        "test_dsss_receiver");
  }
  /* Call the function. */
  test_dsss_receiver_api(SD);
}

/* End of code generation (_coder_test_dsss_receiver_mex.c) */
