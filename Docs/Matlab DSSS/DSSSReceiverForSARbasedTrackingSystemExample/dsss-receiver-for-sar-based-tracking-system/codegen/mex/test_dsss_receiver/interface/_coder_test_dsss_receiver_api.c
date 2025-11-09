/*
 * _coder_test_dsss_receiver_api.c
 *
 * Code generation for function '_coder_test_dsss_receiver_api'
 *
 */

/* Include files */
#include "_coder_test_dsss_receiver_api.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver.h"
#include "test_dsss_receiver_data.h"
#include "test_dsss_receiver_types.h"

/* Function Definitions */
void test_dsss_receiver_api(test_dsss_receiverStackData *SD)
{
  emlrtStack st = {
      NULL, /* site */
      NULL, /* tls */
      NULL  /* prev */
  };
  st.tls = emlrtRootTLSGlobal;
  /* Invoke the target function */
  test_dsss_receiver(SD, &st);
}

/* End of code generation (_coder_test_dsss_receiver_api.c) */
