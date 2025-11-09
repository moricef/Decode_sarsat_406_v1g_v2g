/*
 * test_dsss_receiver_types.h
 *
 * Code generation for function 'test_dsss_receiver'
 *
 */

#pragma once

/* Include files */
#include "rtwtypes.h"
#include "emlrt.h"

/* Type Definitions */
#ifndef typedef_emxArray_creal_T
#define typedef_emxArray_creal_T
typedef struct {
  creal_T *data;
  int32_T *size;
  int32_T allocatedSize;
  int32_T numDimensions;
  boolean_T canFreeData;
} emxArray_creal_T;
#endif /* typedef_emxArray_creal_T */

#ifndef struct_emxArray_real_T
#define struct_emxArray_real_T
struct emxArray_real_T {
  real_T *data;
  int32_T *size;
  int32_T allocatedSize;
  int32_T numDimensions;
  boolean_T canFreeData;
};
#endif /* struct_emxArray_real_T */
#ifndef typedef_emxArray_real_T
#define typedef_emxArray_real_T
typedef struct emxArray_real_T emxArray_real_T;
#endif /* typedef_emxArray_real_T */

#ifndef struct_emxArray_int32_T
#define struct_emxArray_int32_T
struct emxArray_int32_T {
  int32_T *data;
  int32_T *size;
  int32_T allocatedSize;
  int32_T numDimensions;
  boolean_T canFreeData;
};
#endif /* struct_emxArray_int32_T */
#ifndef typedef_emxArray_int32_T
#define typedef_emxArray_int32_T
typedef struct emxArray_int32_T emxArray_int32_T;
#endif /* typedef_emxArray_int32_T */

#ifndef struct_emxArray_boolean_T
#define struct_emxArray_boolean_T
struct emxArray_boolean_T {
  boolean_T *data;
  int32_T *size;
  int32_T allocatedSize;
  int32_T numDimensions;
  boolean_T canFreeData;
};
#endif /* struct_emxArray_boolean_T */
#ifndef typedef_emxArray_boolean_T
#define typedef_emxArray_boolean_T
typedef struct emxArray_boolean_T emxArray_boolean_T;
#endif /* typedef_emxArray_boolean_T */

#ifndef c_typedef_b_helperPolyphaseCorr
#define c_typedef_b_helperPolyphaseCorr
typedef struct {
  real_T xcorrBuffer[308592];
  creal_T x[38574];
  creal_T rxBuffer[38400];
  real_T y[38400];
} b_helperPolyphaseCorrelator;
#endif /* c_typedef_b_helperPolyphaseCorr */

#ifndef typedef_b_pskmod
#define typedef_b_pskmod
typedef struct {
  real_T xMat[6400];
} b_pskmod;
#endif /* typedef_b_pskmod */

#ifndef typedef_b_dsss_receiver
#define typedef_b_dsss_receiver
typedef struct {
  creal_T sampleBuffer[614400];
  creal_T rxAGCSamples[614400];
  creal_T sampleBufferQPSK[614400];
  real_T y[614400];
  real_T corrBuffer[38400];
  creal_T preambleQPSK[3200];
  boolean_T PRN_I[38400];
  boolean_T PRN_Q[38400];
  boolean_T rxCi_data[38400];
  boolean_T rxCq_data[38400];
  boolean_T Ibdn_data[38400];
} b_dsss_receiver;
#endif /* typedef_b_dsss_receiver */

#ifndef typedef_b_test_dsss_receiver
#define typedef_b_test_dsss_receiver
typedef struct {
  creal_T otaBuffer[307200];
  real_T dv[307200];
  real_T dv1[307200];
} b_test_dsss_receiver;
#endif /* typedef_b_test_dsss_receiver */

#ifndef c_typedef_test_dsss_receiverSta
#define c_typedef_test_dsss_receiverSta
typedef struct {
  union {
    b_helperPolyphaseCorrelator f0;
    b_pskmod f1;
  } u1;
  b_dsss_receiver f2;
  b_test_dsss_receiver f3;
} test_dsss_receiverStackData;
#endif /* c_typedef_test_dsss_receiverSta */

/* End of code generation (test_dsss_receiver_types.h) */
