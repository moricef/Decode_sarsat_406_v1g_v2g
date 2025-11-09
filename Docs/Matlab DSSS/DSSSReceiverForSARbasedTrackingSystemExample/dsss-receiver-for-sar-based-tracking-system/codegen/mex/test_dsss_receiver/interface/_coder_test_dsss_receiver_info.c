/*
 * _coder_test_dsss_receiver_info.c
 *
 * Code generation for function 'test_dsss_receiver'
 *
 */

/* Include files */
#include "_coder_test_dsss_receiver_info.h"
#include "emlrt.h"
#include "tmwtypes.h"

/* Function Declarations */
static const mxArray *c_emlrtMexFcnResolvedFunctionsI(void);

/* Function Definitions */
static const mxArray *c_emlrtMexFcnResolvedFunctionsI(void)
{
  const mxArray *nameCaptureInfo;
  const char_T *data[6] = {
      "789ced564d6fd34010dda016f552c8a520fe00520f46a181aa8253d22452515a95d8525b"
      "b1a8ddd893c6c4eb35bbeb9270e2cc8553a55eb8f1b3f80d5cb870a4"
      "f1476cafb44aa4486e2b752ee3d1dbd937fb66e55954d9dbaf20841ea1d876d6639f3854"
      "4dfc03543415af28eb2ac5e56815ad14f252fc47e26de64b18cb38f0",
      "098559a6c3a8eb135f5a93001007c1bc0b702264e07a60b914cc7c70308d682707cd8229"
      "34fdde1d823d32438af85064157af960a6475aa87ade9505f5f8a9d1"
      "a3aae01fda1f5b6f7087f4718bd921055f0abcdfb0ba8d266e8f090d3c10b8b755db7a45"
      "b0cd28c52dd3347b60837b01bcc3b8d9e8f58900c7e2c41eb9feb939",
      "11126892891d2184c193d5c6807143106e4409864c320c11a56009429e4e134ed3841734"
      "a747a039efa27aa8fd57f548f1420539feb325f91f6af96344481eda"
      "32e3fbb724df9596af88df9afe2bad9fa7fbe30575507db67e2df2a3f7bf23a82cbeb74f"
      "9fff29932fb59be21b6bf65bf41e3fd1f05515fcf8a03d22dce992cf",
      "f5da36a1939d41a3499a591d877378e6d581347159fbdfff0f62fbb6a40ecfe6e890e243"
      "f002e087cc9b04c3ebfa7619e7e011c978829735171c16f63dc8f8d6"
      "967c17fcd2f215f15b730fb48d889e07a5fdd7ce4b9e139b97dfff96c997da5d9d131b1a"
      "beaa82d75f766bafadf671fdddd1f697af829d849fd81ebafb73e23f",
      "d7e051d2",
      ""};
  nameCaptureInfo = NULL;
  emlrtNameCaptureMxArrayR2016a(&data[0], 3408U, &nameCaptureInfo);
  return nameCaptureInfo;
}

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
  xInputs = emlrtCreateLogicalMatrix(1, 0);
  emlrtSetField(xEntryPoints, 0, "Name",
                emlrtMxCreateString("test_dsss_receiver"));
  emlrtSetField(xEntryPoints, 0, "NumberOfInputs",
                emlrtMxCreateDoubleScalar(0.0));
  emlrtSetField(xEntryPoints, 0, "NumberOfOutputs",
                emlrtMxCreateDoubleScalar(0.0));
  emlrtSetField(xEntryPoints, 0, "ConstantInputs", xInputs);
  emlrtSetField(
      xEntryPoints, 0, "FullPath",
      emlrtMxCreateString(
          "D:"
          "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSAR"
          "basedTrackingSystemExample\\dsss-receiver-for-sar-based-tr"
          "acking-system\\test_dsss_receiver.m"));
  emlrtSetField(xEntryPoints, 0, "TimeStamp",
                emlrtMxCreateDoubleScalar(739930.52283564815));
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
                emlrtMxCreateString("SCL9QNpDS80n3ddSFMzrf"));
  emlrtSetField(xResult, 0, "EntryPoints", xEntryPoints);
  return xResult;
}

/* End of code generation (_coder_test_dsss_receiver_info.c) */
