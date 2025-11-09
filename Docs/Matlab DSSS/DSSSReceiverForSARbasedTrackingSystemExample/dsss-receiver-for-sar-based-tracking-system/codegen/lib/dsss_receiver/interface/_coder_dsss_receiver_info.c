/*
 * File: _coder_dsss_receiver_info.c
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

/* Include Files */
#include "_coder_dsss_receiver_info.h"
#include "emlrt.h"
#include "tmwtypes.h"

/* Function Declarations */
static const mxArray *c_emlrtMexFcnResolvedFunctionsI(void);

/* Function Definitions */
/*
 * Arguments    : void
 * Return Type  : const mxArray *
 */
static const mxArray *c_emlrtMexFcnResolvedFunctionsI(void)
{
  const mxArray *nameCaptureInfo;
  const char_T *data[5] = {
      "789ccd54cb4ec240149d1a5436281b17fe812e4a10d4853be49160c0206da2c6313ab417"
      "a8763acd4c8be0cab51b57267e809fe55fb871a994965732818488de"
      "cdedc9993be7cc99499152ae2a08a10d34a89df5414f843819f6153459d3bc22e951ada2"
      "d8c45cc4bf84dd608e075d6f001c42613869326a39c4f1f49e0b8883",
      "607607cc80695a36e816056d1c9cf6112d8d5143d0a7fadff93618f79a4f116f8b91437b"
      "1c0cf3f8929c3736671e6f923c9253fc55f1ba70844ba4810bccf029"
      "389ec0d59c5ec91de3629750d70681eb9974669f6083518a0b9aa6d5c100ab03bcc4b896"
      "ab37880053e7c4b8b79c96d6131ed070129b42089587abd526e3aa20",
      "5c0d06542f9c50453012acbd89d6a66894c3d382396ccfc821e2db60bbc06bcceeb9ed1f"
      "7f79c639d8c4633ce4233fb70bfa5993fa193026f31b368cf4e2ca62"
      "7aef52bd49fedfbc03e945a468dfe7ac7bd89c3317d9ff2281e2416f9d7d28cbd4db7d7d"
      "fe5ca65e547fa5d795ec37efbbde92e825a7f8ec5e257da0172fb227",
      "e7870f8f825dfa77ac3ce6a3364367960f24c1bfbdff37de6dbf6e", ""};
  nameCaptureInfo = NULL;
  emlrtNameCaptureMxArrayR2016a(&data[0], 1856U, &nameCaptureInfo);
  return nameCaptureInfo;
}

/*
 * Arguments    : void
 * Return Type  : mxArray *
 */
mxArray *emlrtMexFcnProperties(void)
{
  mxArray *xEntryPoints;
  mxArray *xInputs;
  mxArray *xResult;
  const char_T *propFieldName[9] = {"Version",
                                    "ResolvedFunctions",
                                    "Checksum",
                                    "EntryPoints",
                                    "CoverageInfo",
                                    "IsPolymorphic",
                                    "PropertyList",
                                    "UUID",
                                    "ClassEntryPointIsHandle"};
  const char_T *epFieldName[8] = {
      "Name",     "NumberOfInputs", "NumberOfOutputs", "ConstantInputs",
      "FullPath", "TimeStamp",      "Constructor",     "Visible"};
  xEntryPoints =
      emlrtCreateStructMatrix(1, 1, 8, (const char_T **)&epFieldName[0]);
  xInputs = emlrtCreateLogicalMatrix(1, 2);
  emlrtSetField(xEntryPoints, 0, "Name", emlrtMxCreateString("dsss_receiver"));
  emlrtSetField(xEntryPoints, 0, "NumberOfInputs",
                emlrtMxCreateDoubleScalar(2.0));
  emlrtSetField(xEntryPoints, 0, "NumberOfOutputs",
                emlrtMxCreateDoubleScalar(3.0));
  emlrtSetField(xEntryPoints, 0, "ConstantInputs", xInputs);
  emlrtSetField(
      xEntryPoints, 0, "FullPath",
      emlrtMxCreateString(
          "D:"
          "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSAR"
          "basedTrackingSystemExample\\dsss-receiver-for-sar-based-tr"
          "acking-system\\dsss_receiver.m"));
  emlrtSetField(xEntryPoints, 0, "TimeStamp",
                emlrtMxCreateDoubleScalar(739930.51957175927));
  emlrtSetField(xEntryPoints, 0, "Constructor",
                emlrtMxCreateLogicalScalar(false));
  emlrtSetField(xEntryPoints, 0, "Visible", emlrtMxCreateLogicalScalar(true));
  xResult =
      emlrtCreateStructMatrix(1, 1, 9, (const char_T **)&propFieldName[0]);
  emlrtSetField(xResult, 0, "Version",
                emlrtMxCreateString("24.1.0.2537033 (R2024a)"));
  emlrtSetField(xResult, 0, "ResolvedFunctions",
                (mxArray *)c_emlrtMexFcnResolvedFunctionsI());
  emlrtSetField(xResult, 0, "Checksum",
                emlrtMxCreateString("BDvH36SiUxbVFfPxdOEExF"));
  emlrtSetField(xResult, 0, "EntryPoints", xEntryPoints);
  return xResult;
}

/*
 * File trailer for _coder_dsss_receiver_info.c
 *
 * [EOF]
 */
