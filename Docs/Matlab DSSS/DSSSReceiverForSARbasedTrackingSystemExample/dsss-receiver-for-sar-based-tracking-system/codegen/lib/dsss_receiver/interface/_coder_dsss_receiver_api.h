/*
 * File: _coder_dsss_receiver_api.h
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

#ifndef _CODER_DSSS_RECEIVER_API_H
#define _CODER_DSSS_RECEIVER_API_H

/* Include Files */
#include "emlrt.h"
#include "mex.h"
#include "tmwtypes.h"
#include <string.h>

/* Type Definitions */
#ifndef typedef_struct0_T
#define typedef_struct0_T
typedef struct {
  real_T oversampling;
  real_T velocity;
  real_T txPower;
} struct0_T;
#endif /* typedef_struct0_T */

/* Variable Declarations */
extern emlrtCTX emlrtRootTLSGlobal;
extern emlrtContext emlrtContextGlobal;

#ifdef __cplusplus
extern "C" {
#endif

/* Function Declarations */
void dsss_receiver(creal_T otaBuffer[307200], struct0_T *settings,
                   boolean_T rxPayload[202], int32_T *errs,
                   boolean_T *rxStatus);

void dsss_receiver_api(const mxArray *const prhs[2], int32_T nlhs,
                       const mxArray *plhs[3]);

void dsss_receiver_atexit(void);

void dsss_receiver_initialize(void);

void dsss_receiver_terminate(void);

void dsss_receiver_xil_shutdown(void);

void dsss_receiver_xil_terminate(void);

#ifdef __cplusplus
}
#endif

#endif
/*
 * File trailer for _coder_dsss_receiver_api.h
 *
 * [EOF]
 */
