/*
 * dsss_receiver.c
 *
 * Code generation for function 'dsss_receiver'
 *
 */

/* Include files */
#include "dsss_receiver.h"
#include "exp.h"
#include "helperPolyphaseCorrelator.h"
#include "indexShapeCheck.h"
#include "pskdemod.h"
#include "pskmod.h"
#include "rt_nonfinite.h"
#include "sign.h"
#include "sumMatrixIncludeNaN.h"
#include "test_dsss_receiver_data.h"
#include "test_dsss_receiver_emxutil.h"
#include "test_dsss_receiver_types.h"
#include "mwmathutil.h"
#include <string.h>

/* Variable Definitions */
static emlrtRSInfo f_emlrtRSI = {
    222,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo g_emlrtRSI = {
    197,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo h_emlrtRSI = {
    196,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo i_emlrtRSI = {
    153,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo j_emlrtRSI = {
    152,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo k_emlrtRSI = {
    138,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo l_emlrtRSI = {
    134,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo m_emlrtRSI = {
    133,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo n_emlrtRSI = {
    132,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo o_emlrtRSI = {
    127,             /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo p_emlrtRSI = {
    70,              /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo q_emlrtRSI = {
    47,              /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo r_emlrtRSI = {
    36,              /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtBCInfo emlrtBCI = {
    1,             /* iFirst */
    202,           /* iLast */
    335,           /* lineNo */
    23,            /* colNo */
    "decodedBits", /* aName */
    "bchDecoder",  /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtRTEInfo emlrtRTEI = {
    13,               /* lineNo */
    13,               /* colNo */
    "toLogicalCheck", /* fName */
    "C:\\Program "
    "Files\\MATLAB\\R2024a\\toolbox\\eml\\eml\\+coder\\+"
    "internal\\toLogicalCheck.m" /* pName */
};

static emlrtECInfo emlrtECI = {
    -1,              /* nDims */
    53,              /* lineNo */
    1,               /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo b_emlrtRTEI = {
    151,             /* lineNo */
    17,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtBCInfo b_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    152,             /* lineNo */
    41,              /* colNo */
    "rxBits",        /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo c_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    153,             /* lineNo */
    41,              /* colNo */
    "rxBits",        /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo d_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    175,             /* lineNo */
    28,              /* colNo */
    "rxCi",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo e_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    176,             /* lineNo */
    28,              /* colNo */
    "rxCq",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo f_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    196,             /* lineNo */
    44,              /* colNo */
    "Ibdn",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo g_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    196,             /* lineNo */
    57,              /* colNo */
    "Ibdn",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo h_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    197,             /* lineNo */
    44,              /* colNo */
    "Qbdn",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo i_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    197,             /* lineNo */
    57,              /* colNo */
    "Qbdn",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo j_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    196,             /* lineNo */
    27,              /* colNo */
    "I_reshaped",    /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtECInfo b_emlrtECI = {
    -1,              /* nDims */
    196,             /* lineNo */
    13,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtBCInfo k_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    206,             /* lineNo */
    42,              /* colNo */
    "I_reshaped",    /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo l_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    197,             /* lineNo */
    27,              /* colNo */
    "Q_reshaped",    /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtECInfo c_emlrtECI = {
    -1,              /* nDims */
    197,             /* lineNo */
    13,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtBCInfo m_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    207,             /* lineNo */
    42,              /* colNo */
    "Q_reshaped",    /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo n_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    122,             /* lineNo */
    17,              /* colNo */
    "rxBurst",       /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo o_emlrtBCI = {
    -1,                   /* iFirst */
    -1,                   /* iLast */
    324,                  /* lineNo */
    31,                   /* colNo */
    "input",              /* aName */
    "symbolSynchronizer", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo p_emlrtBCI = {
    -1,                   /* iFirst */
    -1,                   /* iLast */
    324,                  /* lineNo */
    20,                   /* colNo */
    "output",             /* aName */
    "symbolSynchronizer", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo q_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    152,             /* lineNo */
    18,              /* colNo */
    "rxCi",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo r_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    153,             /* lineNo */
    18,              /* colNo */
    "rxCq",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo s_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    175,             /* lineNo */
    14,              /* colNo */
    "Ibdn",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo t_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    176,             /* lineNo */
    14,              /* colNo */
    "Qbdn",          /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo u_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    206,             /* lineNo */
    19,              /* colNo */
    "Ibits",         /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo v_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    213,             /* lineNo */
    44,              /* colNo */
    "Ibits",         /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo w_emlrtBCI = {
    -1,                /* iFirst */
    -1,                /* iLast */
    213,               /* lineNo */
    29,                /* colNo */
    "despreadMessage", /* aName */
    "dsss_receiver",   /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo x_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    207,             /* lineNo */
    19,              /* colNo */
    "Qbits",         /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo y_emlrtBCI = {
    -1,              /* iFirst */
    -1,              /* iLast */
    214,             /* lineNo */
    42,              /* colNo */
    "Qbits",         /* aName */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo ab_emlrtBCI = {
    -1,                /* iFirst */
    -1,                /* iLast */
    214,               /* lineNo */
    29,                /* colNo */
    "despreadMessage", /* aName */
    "dsss_receiver",   /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo bb_emlrtBCI = {
    -1,           /* iFirst */
    -1,           /* iLast */
    333,          /* lineNo */
    40,           /* colNo */
    "inputBits",  /* aName */
    "bchDecoder", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo cb_emlrtBCI = {
    -1,                           /* iFirst */
    -1,                           /* iLast */
    277,                          /* lineNo */
    9,                            /* colNo */
    "input",                      /* aName */
    "coarseFrequencyCompensator", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo fb_emlrtBCI = {
    -1,                    /* iFirst */
    -1,                    /* iLast */
    299,                   /* lineNo */
    51,                    /* colNo */
    "output",              /* aName */
    "carrierSynchronizer", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo gb_emlrtBCI = {
    -1,                    /* iFirst */
    -1,                    /* iLast */
    296,                   /* lineNo */
    9,                     /* colNo */
    "input",               /* aName */
    "carrierSynchronizer", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtBCInfo hb_emlrtBCI = {
    -1,                    /* iFirst */
    -1,                    /* iLast */
    299,                   /* lineNo */
    27,                    /* colNo */
    "output",              /* aName */
    "carrierSynchronizer", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m", /* pName */
    0                                 /* checkKind */
};

static emlrtRTEInfo g_emlrtRTEI = {
    1,               /* lineNo */
    40,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo h_emlrtRTEI = {
    53,              /* lineNo */
    35,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo i_emlrtRTEI = {
    116,             /* lineNo */
    1,               /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo j_emlrtRTEI = {
    127,             /* lineNo */
    28,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo k_emlrtRTEI = {
    132,             /* lineNo */
    1,               /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo l_emlrtRTEI = {
    134,             /* lineNo */
    1,               /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo m_emlrtRTEI = {
    138,             /* lineNo */
    5,               /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo n_emlrtRTEI = {
    53,              /* lineNo */
    14,              /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRTEInfo o_emlrtRTEI = {
    288,             /* lineNo */
    5,               /* colNo */
    "dsss_receiver", /* fName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pName */
};

static emlrtRSInfo fc_emlrtRSI = {
    32,              /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

static emlrtRSInfo gc_emlrtRSI = {
    31,              /* lineNo */
    "dsss_receiver", /* fcnName */
    "D:"
    "\\Fab\\Documents\\MATLAB\\Examples\\R2024a\\comm\\DSSSReceiverForSARbasedT"
    "rackingSystemExample\\dsss-receiver-for-sar-based-tr"
    "acking-system\\dsss_receiver.m" /* pathName */
};

/* Function Declarations */
static void carrierSynchronizer(const emlrtStack *sp,
                                const emxArray_creal_T *input,
                                emxArray_creal_T *output);

static void generatePNSequence(const emlrtStack *sp,
                               const real_T initialConditions[23],
                               boolean_T pnSeq[38400]);

/* Function Definitions */
static void carrierSynchronizer(const emlrtStack *sp,
                                const emxArray_creal_T *input,
                                emxArray_creal_T *output)
{
  const creal_T *input_data;
  creal_T *output_data;
  real_T freq;
  real_T phase;
  int32_T i;
  int32_T loop_ub;
  input_data = input->data;
  loop_ub = input->size[0];
  i = output->size[0];
  output->size[0] = input->size[0];
  emxEnsureCapacity_creal_T(sp, output, i, &o_emlrtRTEI);
  output_data = output->data;
  for (i = 0; i < loop_ub; i++) {
    output_data[i].re = 0.0;
    output_data[i].im = 0.0;
  }
  phase = 0.0;
  freq = 0.0;
  for (i = 0; i < loop_ub; i++) {
    real_T b_error;
    real_T b_y_re_tmp;
    real_T y_im;
    real_T y_re_tmp;
    if (phase * 0.0 == 0.0) {
      b_error = muDoubleScalarCos(-phase);
      y_im = muDoubleScalarSin(-phase);
    } else if (-phase == 0.0) {
      b_error = rtNaN;
      y_im = 0.0;
    } else {
      b_error = rtNaN;
      y_im = rtNaN;
    }
    if (i + 1 > loop_ub) {
      emlrtDynamicBoundsCheckR2012b(i + 1, 1, loop_ub, &gb_emlrtBCI,
                                    (emlrtConstCTX)sp);
    }
    y_re_tmp = input_data[i].re;
    b_y_re_tmp = input_data[i].im;
    output_data[i].re = y_re_tmp * b_error - b_y_re_tmp * y_im;
    if (i + 1 > loop_ub) {
      emlrtDynamicBoundsCheckR2012b(i + 1, 1, loop_ub, &gb_emlrtBCI,
                                    (emlrtConstCTX)sp);
    }
    output_data[i].im = y_re_tmp * y_im + b_y_re_tmp * b_error;
    if (i + 1 > 1) {
      if ((i < 1) || (i > loop_ub)) {
        emlrtDynamicBoundsCheckR2012b(i, 1, loop_ub, &fb_emlrtBCI,
                                      (emlrtConstCTX)sp);
      }
      b_error = output_data[i - 1].re;
      if (i > loop_ub) {
        emlrtDynamicBoundsCheckR2012b(i, 1, loop_ub, &fb_emlrtBCI,
                                      (emlrtConstCTX)sp);
      }
      y_im = -output_data[i - 1].im;
      if (i + 1 > loop_ub) {
        emlrtDynamicBoundsCheckR2012b(i + 1, 1, loop_ub, &hb_emlrtBCI,
                                      (emlrtConstCTX)sp);
      }
      y_re_tmp = output_data[i].re;
      b_y_re_tmp = output_data[i].im;
      if (i + 1 > loop_ub) {
        emlrtDynamicBoundsCheckR2012b(i + 1, 1, loop_ub, &hb_emlrtBCI,
                                      (emlrtConstCTX)sp);
      }
      b_error = muDoubleScalarAtan2(y_re_tmp * y_im + b_y_re_tmp * b_error,
                                    y_re_tmp * b_error - b_y_re_tmp * y_im) /
                4.0;
    } else {
      b_error = 0.0;
    }
    freq += 0.001 * b_error;
    phase = (phase + freq) + 0.01 * b_error;
    if (phase > 3.1415926535897931) {
      phase -= 6.2831853071795862;
    } else if (phase < -3.1415926535897931) {
      phase += 6.2831853071795862;
    }
    if (*emlrtBreakCheckR2012bFlagVar != 0) {
      emlrtBreakCheckR2012b((emlrtConstCTX)sp);
    }
  }
}

static void generatePNSequence(const emlrtStack *sp,
                               const real_T initialConditions[23],
                               boolean_T pnSeq[38400])
{
  real_T b_reg[23];
  real_T reg[23];
  int32_T i;
  /*  Fonctions codegen-compatibles (inchangées) */
  memcpy(&reg[0], &initialConditions[0], 23U * sizeof(real_T));
  for (i = 0; i < 38400; i++) {
    pnSeq[i] = (reg[22] != 0.0);
    /*  Polynomial: X^23 + X^18 + 1 */
    b_reg[0] = ((reg[22] != 0.0) != (reg[17] != 0.0));
    memcpy(&b_reg[1], &reg[0], 22U * sizeof(real_T));
    memcpy(&reg[0], &b_reg[0], 23U * sizeof(real_T));
    if (*emlrtBreakCheckR2012bFlagVar != 0) {
      emlrtBreakCheckR2012b((emlrtConstCTX)sp);
    }
  }
}

int32_T dsss_receiver(test_dsss_receiverStackData *SD, const emlrtStack *sp,
                      const creal_T otaBuffer[307200], boolean_T rxPayload[202],
                      boolean_T *rxStatus)
{
  static const real_T dv[23] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  static const real_T dv1[23] = {0.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 1.0,
                                 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0,
                                 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0};
  emlrtStack st;
  emxArray_boolean_T *x;
  emxArray_creal_T *coarseSyncOut;
  emxArray_creal_T *r2;
  emxArray_creal_T *rxBurst;
  emxArray_creal_T *syncedQPSK;
  emxArray_int32_T *r;
  emxArray_real_T *rxBits;
  creal_T dc;
  creal_T *coarseSyncOut_data;
  creal_T *rxBurst_data;
  real_T avgPower;
  real_T startSampIdx_data;
  real_T *rxBits_data;
  int32_T startSampIdx_size[2];
  int32_T b_i;
  int32_T errs;
  int32_T i;
  int32_T i1;
  int32_T loop_ub;
  int32_T outputLength;
  int32_T sampleIndex;
  int32_T *r1;
  int8_T I_reshaped_data[38400];
  int8_T Q_reshaped_data[38400];
  int8_T tmp_data[38400];
  boolean_T PRN_I[6400];
  boolean_T despreadMessage_data[300];
  boolean_T *x_data;
  st.prev = sp;
  st.tls = sp->tls;
  emlrtHeapReferenceStackEnterFcnR2012b((emlrtConstCTX)sp);
  /*  DSSS_RECEIVER Implémente un récepteur DSSS COSPAS-SARSAT */
  /*  Constantes */
  /*  m/s */
  /*  Paramètres système */
  /*  MHz */
  /*  bits */
  /*  chips/s */
  /*  facteur d'étalement */
  /*  Calculs dérivés */
  /*  Initialisation des séquences PRN */
  /*  Génération des séquences PRN avec fonctions codegen-compatibles */
  st.site = &gc_emlrtRSI;
  generatePNSequence(&st, dv, SD->f2.PRN_I);
  st.site = &fc_emlrtRSI;
  generatePNSequence(&st, dv1, SD->f2.PRN_Q);
  /*  Génération du préambule QPSK */
  for (i = 0; i < 3200; i++) {
    sampleIndex = i << 1;
    PRN_I[sampleIndex] = SD->f2.PRN_I[i];
    PRN_I[sampleIndex + 1] = SD->f2.PRN_Q[i];
  }
  st.site = &r_emlrtRSI;
  pskmod(SD, &st, PRN_I, SD->f2.preambleQPSK);
  /*  Buffer d'échantillons */
  memset(&SD->f2.sampleBuffer[0], 0, 614400U * sizeof(creal_T));
  memcpy(&SD->f2.sampleBuffer[0], &otaBuffer[0], 307200U * sizeof(creal_T));
  /*  AGC codegen-compatible */
  st.site = &q_emlrtRSI;
  avgPower = 0.0;
  for (b_i = 0; b_i < 614400; b_i++) {
    if (b_i + 1 == 1) {
      startSampIdx_data = muDoubleScalarHypot(SD->f2.sampleBuffer[0].re,
                                              SD->f2.sampleBuffer[0].im);
      avgPower = startSampIdx_data * startSampIdx_data;
    } else if (muDoubleScalarRem((real_T)b_i + 1.0, 80.0) == 1.0) {
      startSampIdx_data = muDoubleScalarHypot(SD->f2.sampleBuffer[b_i].re,
                                              SD->f2.sampleBuffer[b_i].im);
      avgPower = 0.9 * avgPower + 0.1 * (startSampIdx_data * startSampIdx_data);
    }
    if (avgPower > 0.0) {
      startSampIdx_data = 1.0 / muDoubleScalarSqrt(avgPower);
    } else {
      startSampIdx_data = 1.0;
    }
    SD->f2.rxAGCSamples[b_i].re =
        startSampIdx_data * SD->f2.sampleBuffer[b_i].re;
    SD->f2.rxAGCSamples[b_i].im =
        startSampIdx_data * SD->f2.sampleBuffer[b_i].im;
    if (*emlrtBreakCheckR2012bFlagVar != 0) {
      emlrtBreakCheckR2012b(&st);
    }
  }
  /*  Conversion explicite en double et saturation */
  sampleIndex = 0;
  for (outputLength = 0; outputLength < 614400; outputLength++) {
    avgPower = muDoubleScalarHypot(SD->f2.rxAGCSamples[outputLength].re,
                                   SD->f2.rxAGCSamples[outputLength].im);
    SD->f2.y[outputLength] = avgPower;
    if (avgPower > 1.2) {
      sampleIndex++;
    }
  }
  emxInit_int32_T(sp, &r, &n_emlrtRTEI);
  i = r->size[0];
  r->size[0] = sampleIndex;
  emxEnsureCapacity_int32_T(sp, r, i, &g_emlrtRTEI);
  r1 = r->data;
  sampleIndex = 0;
  for (b_i = 0; b_i < 614400; b_i++) {
    if (SD->f2.y[b_i] > 1.2) {
      r1[sampleIndex] = b_i;
      sampleIndex++;
    }
  }
  emxInit_creal_T(sp, &r2, &h_emlrtRTEI);
  loop_ub = r->size[0];
  i = r2->size[0];
  r2->size[0] = r->size[0];
  emxEnsureCapacity_creal_T(sp, r2, i, &h_emlrtRTEI);
  coarseSyncOut_data = r2->data;
  for (i = 0; i < loop_ub; i++) {
    coarseSyncOut_data[i] = SD->f2.rxAGCSamples[r1[i]];
  }
  b_sign(r2);
  coarseSyncOut_data = r2->data;
  outputLength = r2->size[0];
  for (i = 0; i < outputLength; i++) {
    coarseSyncOut_data[i].re *= 1.2;
    coarseSyncOut_data[i].im *= 1.2;
  }
  if (r->size[0] != r2->size[0]) {
    emlrtSubAssignSizeCheck1dR2017a(r->size[0], r2->size[0], &emlrtECI,
                                    (emlrtConstCTX)sp);
  }
  for (i = 0; i < outputLength; i++) {
    SD->f2.rxAGCSamples[r1[i]] = coarseSyncOut_data[i];
  }
  emxFree_creal_T(sp, &r2);
  emxFree_int32_T(sp, &r);
  /*  Détection du préambule */
  /*  Préparation du buffer QPSK avec taille fixe */
  memset(&SD->f2.sampleBufferQPSK[0], 0, 614400U * sizeof(creal_T));
  for (i = 0; i < 614396; i++) {
    avgPower = SD->f2.rxAGCSamples[i + 4].im;
    SD->f2.sampleBufferQPSK[i].re = SD->f2.rxAGCSamples[i].re + 0.0 * avgPower;
    SD->f2.sampleBufferQPSK[i].im = avgPower;
  }
  st.site = &p_emlrtRSI;
  helperPolyphaseCorrelator(
      SD, &st, &SD->f2.sampleBufferQPSK[0], &SD->f2.preambleQPSK[200],
      (real_T *)&startSampIdx_data, startSampIdx_size, SD->f2.corrBuffer);
  /*  Extraction du burst */
  memset(&rxPayload[0], 0, 202U * sizeof(boolean_T));
  errs = 0;
  *rxStatus = false;
  emxInit_creal_T(sp, &rxBurst, &i_emlrtRTEI);
  emxInit_creal_T(sp, &coarseSyncOut, &k_emlrtRTEI);
  emxInit_creal_T(sp, &syncedQPSK, &l_emlrtRTEI);
  emxInit_real_T(sp, &rxBits, &m_emlrtRTEI);
  emxInit_boolean_T(sp, &x, &j_emlrtRTEI);
  if ((startSampIdx_size[0] != 0) && (startSampIdx_size[1] != 0)) {
    real_T burstLength_tmp;
    boolean_T exitg1;
    boolean_T y;
    /*  Vérification que startSampIdx est un scalaire */
    /*  Calcul des indices avec vérification des limites */
    /*  Conversion explicite en double */
    /*  Vérification que les indices sont valides (version corrigée) */
    /*  Extraction des valeurs scalaires */
    /*  Conditions de validation */
    /*  Utilisation des indices scalaires pour l'extraction */
    /*  Extraction sécurisée du burst avec pré-allocation */
    burstLength_tmp =
        (((startSampIdx_data + 307360.0) - 1.0) - startSampIdx_data) + 1.0;
    outputLength = (int32_T)burstLength_tmp;
    i = rxBurst->size[0];
    rxBurst->size[0] = (int32_T)burstLength_tmp;
    emxEnsureCapacity_creal_T(sp, rxBurst, i, &i_emlrtRTEI);
    rxBurst_data = rxBurst->data;
    for (i = 0; i < outputLength; i++) {
      rxBurst_data[i].re = 0.0;
      rxBurst_data[i].im = 0.0;
    }
    /*  Copie manuelle des échantillons */
    for (b_i = 0; b_i < outputLength; b_i++) {
      avgPower = (startSampIdx_data + ((real_T)b_i + 1.0)) - 1.0;
      if (avgPower <= 614400.0) {
        if (((int32_T)((uint32_T)b_i + 1U) < 1) ||
            ((int32_T)((uint32_T)b_i + 1U) > (int32_T)burstLength_tmp)) {
          emlrtDynamicBoundsCheckR2012b((int32_T)((uint32_T)b_i + 1U), 1,
                                        (int32_T)burstLength_tmp, &n_emlrtBCI,
                                        (emlrtConstCTX)sp);
        }
        rxBurst_data[b_i].re = SD->f2.sampleBuffer[(int32_T)avgPower - 1].re;
        rxBurst_data[b_i].im = SD->f2.sampleBuffer[(int32_T)avgPower - 1].im;
      }
      if (*emlrtBreakCheckR2012bFlagVar != 0) {
        emlrtBreakCheckR2012b((emlrtConstCTX)sp);
      }
    }
    /*  Vérification que rxBurst n'est pas vide */
    st.site = &o_emlrtRSI;
    i = x->size[0];
    x->size[0] = (int32_T)burstLength_tmp;
    emxEnsureCapacity_boolean_T(&st, x, i, &j_emlrtRTEI);
    x_data = x->data;
    for (i = 0; i < outputLength; i++) {
      x_data[i] = ((rxBurst_data[i].re == 0.0) && (rxBurst_data[i].im == 0.0));
    }
    y = true;
    sampleIndex = 1;
    exitg1 = false;
    while ((!exitg1) && (sampleIndex <= x->size[0])) {
      if (!x_data[sampleIndex - 1]) {
        y = false;
        exitg1 = true;
      } else {
        sampleIndex++;
      }
    }
    if (!y) {
      int32_T Qbdn_size;
      int32_T ex;
      boolean_T Qbdn_data[38400];
      /*  Correction de fréquence et phase */
      st.site = &n_emlrtRSI;
      i = coarseSyncOut->size[0];
      coarseSyncOut->size[0] = (int32_T)burstLength_tmp;
      emxEnsureCapacity_creal_T(&st, coarseSyncOut, i, &k_emlrtRTEI);
      coarseSyncOut_data = coarseSyncOut->data;
      for (i = 0; i < outputLength; i++) {
        coarseSyncOut_data[i].re = 0.0;
        coarseSyncOut_data[i].im = 0.0;
      }
      startSampIdx_data = 0.0;
      for (b_i = 0; b_i < outputLength; b_i++) {
        real_T d;
        dc.re = startSampIdx_data * 0.0;
        dc.im = -startSampIdx_data;
        b_exp(&dc);
        if (b_i + 1 > (int32_T)burstLength_tmp) {
          emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, (int32_T)burstLength_tmp,
                                        &cb_emlrtBCI, &st);
        }
        avgPower = rxBurst_data[b_i].re;
        d = rxBurst_data[b_i].im;
        coarseSyncOut_data[b_i].re = avgPower * dc.re - d * dc.im;
        if (b_i + 1 > (int32_T)burstLength_tmp) {
          emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, (int32_T)burstLength_tmp,
                                        &cb_emlrtBCI, &st);
        }
        coarseSyncOut_data[b_i].im = avgPower * dc.im + d * dc.re;
        startSampIdx_data += 2.0453077171808549E-8;
        if (startSampIdx_data > 6.2831853071795862) {
          startSampIdx_data -= 6.2831853071795862;
        }
        if (*emlrtBreakCheckR2012bFlagVar != 0) {
          emlrtBreakCheckR2012b(&st);
        }
      }
      st.site = &m_emlrtRSI;
      carrierSynchronizer(&st, coarseSyncOut, rxBurst);
      rxBurst_data = rxBurst->data;
      st.site = &l_emlrtRSI;
      outputLength =
          (int32_T)muDoubleScalarFloor((real_T)rxBurst->size[0] / 8.0) - 1;
      i = syncedQPSK->size[0];
      syncedQPSK->size[0] = outputLength + 1;
      emxEnsureCapacity_creal_T(&st, syncedQPSK, i, &l_emlrtRTEI);
      coarseSyncOut_data = syncedQPSK->data;
      for (i = 0; i <= outputLength; i++) {
        coarseSyncOut_data[i].re = 0.0;
        coarseSyncOut_data[i].im = 0.0;
      }
      for (b_i = 0; b_i <= outputLength; b_i++) {
        sampleIndex = (b_i << 3) + 4;
        if (sampleIndex <= rxBurst->size[0]) {
          if (b_i + 1 > syncedQPSK->size[0]) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, syncedQPSK->size[0],
                                          &p_emlrtBCI, &st);
          }
          if (sampleIndex > rxBurst->size[0]) {
            emlrtDynamicBoundsCheckR2012b(sampleIndex, 1, rxBurst->size[0],
                                          &o_emlrtBCI, &st);
          }
          coarseSyncOut_data[b_i].re = rxBurst_data[sampleIndex - 1].re;
          if (sampleIndex > rxBurst->size[0]) {
            emlrtDynamicBoundsCheckR2012b(sampleIndex, 1, rxBurst->size[0],
                                          &o_emlrtBCI, &st);
          }
          if (b_i + 1 > syncedQPSK->size[0]) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, syncedQPSK->size[0],
                                          &p_emlrtBCI, &st);
          }
          coarseSyncOut_data[b_i].im = rxBurst_data[sampleIndex - 1].im;
        }
        if (*emlrtBreakCheckR2012bFlagVar != 0) {
          emlrtBreakCheckR2012b(&st);
        }
      }
      /*  Démodulation et désétalement */
      st.site = &k_emlrtRSI;
      pskdemod(&st, syncedQPSK, rxBits);
      rxBits_data = rxBits->data;
      /*  Extraction des bits I et Q avec vérification de taille */
      /*  Pré-allocation des vecteurs I et Q */
      sampleIndex = (int32_T)muDoubleScalarMin(rxBits->size[0], 76800.0);
      loop_ub = (int32_T)muDoubleScalarCeil((real_T)sampleIndex / 2.0);
      if (loop_ub - 1 >= 0) {
        memset(&SD->f2.rxCi_data[0], 0, (uint32_T)loop_ub * sizeof(boolean_T));
        memset(&SD->f2.rxCq_data[0], 0, (uint32_T)loop_ub * sizeof(boolean_T));
      }
      /*  Remplissage des bits I et Q */
      i = (int32_T)((((real_T)sampleIndex - 1.0) + 1.0) / 2.0);
      emlrtForLoopVectorCheckR2021a(1.0, 2.0, (real_T)sampleIndex - 1.0,
                                    mxDOUBLE_CLASS, i, &b_emlrtRTEI,
                                    (emlrtConstCTX)sp);
      for (b_i = 0; b_i < i; b_i++) {
        sampleIndex = (b_i << 1) + 1;
        if (sampleIndex > rxBits->size[0]) {
          emlrtDynamicBoundsCheckR2012b(sampleIndex, 1, rxBits->size[0],
                                        &b_emlrtBCI, (emlrtConstCTX)sp);
        }
        st.site = &j_emlrtRSI;
        avgPower = rxBits_data[sampleIndex - 1];
        if (muDoubleScalarIsNaN(avgPower)) {
          emlrtErrorWithMessageIdR2018a(&st, &emlrtRTEI, "MATLAB:nologicalnan",
                                        "MATLAB:nologicalnan", 0);
        }
        if (b_i + 1 > loop_ub) {
          emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, loop_ub, &q_emlrtBCI,
                                        (emlrtConstCTX)sp);
        }
        SD->f2.rxCi_data[b_i] = (avgPower != 0.0);
        if (sampleIndex + 1 > rxBits->size[0]) {
          emlrtDynamicBoundsCheckR2012b(sampleIndex + 1, 1, rxBits->size[0],
                                        &c_emlrtBCI, (emlrtConstCTX)sp);
        }
        st.site = &i_emlrtRSI;
        if (muDoubleScalarIsNaN(rxBits_data[sampleIndex])) {
          emlrtErrorWithMessageIdR2018a(&st, &emlrtRTEI, "MATLAB:nologicalnan",
                                        "MATLAB:nologicalnan", 0);
        }
        if (b_i + 1 > loop_ub) {
          emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, loop_ub, &r_emlrtBCI,
                                        (emlrtConstCTX)sp);
        }
        SD->f2.rxCq_data[b_i] = (rxBits_data[sampleIndex] != 0.0);
        if (*emlrtBreakCheckR2012bFlagVar != 0) {
          emlrtBreakCheckR2012b((emlrtConstCTX)sp);
        }
      }
      /*  Désétalement avec vérification de taille */
      ex = loop_ub;
      if (loop_ub > (uint16_T)loop_ub) {
        ex = (uint16_T)loop_ub;
      }
      if (ex > 0) {
        /*  Pré-allocation des résultats du désétalement */
        /*  Désétalement manuel */
        Qbdn_size = ex;
        for (b_i = 0; b_i < ex; b_i++) {
          if (b_i + 1 > loop_ub) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, loop_ub, &d_emlrtBCI,
                                          (emlrtConstCTX)sp);
          }
          if (b_i + 1 > ex) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, ex, &s_emlrtBCI,
                                          (emlrtConstCTX)sp);
          }
          SD->f2.Ibdn_data[b_i] = SD->f2.rxCi_data[b_i] ^ SD->f2.PRN_I[b_i];
          if (b_i + 1 > loop_ub) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, loop_ub, &e_emlrtBCI,
                                          (emlrtConstCTX)sp);
          }
          if (b_i + 1 > ex) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, ex, &t_emlrtBCI,
                                          (emlrtConstCTX)sp);
          }
          Qbdn_data[b_i] = SD->f2.rxCq_data[b_i] ^ SD->f2.PRN_Q[b_i];
          if (*emlrtBreakCheckR2012bFlagVar != 0) {
            emlrtBreakCheckR2012b((emlrtConstCTX)sp);
          }
        }
      } else {
        ex = 0;
        Qbdn_size = 0;
      }
      /*  Décodage ML */
      if ((ex >= 256) && (Qbdn_size >= 256)) {
        int32_T numSymbols_tmp;
        boolean_T Ibits_data[150];
        boolean_T Qbits_data[150];
        numSymbols_tmp = (int32_T)muDoubleScalarMin(
            muDoubleScalarFloor((real_T)ex / 256.0),
            muDoubleScalarFloor((real_T)Qbdn_size / 256.0));
        /*  Pré-allocation pour le reshape */
        outputLength = numSymbols_tmp << 8;
        memset(&I_reshaped_data[0], 0, (uint32_T)outputLength * sizeof(int8_T));
        memset(&Q_reshaped_data[0], 0, (uint32_T)outputLength * sizeof(int8_T));
        /*  Remplissage manuel des matrices */
        for (b_i = 0; b_i < numSymbols_tmp; b_i++) {
          sampleIndex = (b_i << 8) + 1;
          outputLength = (b_i + 1) << 8;
          if (sampleIndex > outputLength) {
            i = 0;
            i1 = 0;
          } else {
            if (sampleIndex > ex) {
              emlrtDynamicBoundsCheckR2012b(sampleIndex, 1, ex, &f_emlrtBCI,
                                            (emlrtConstCTX)sp);
            }
            i = sampleIndex - 1;
            if (outputLength > ex) {
              emlrtDynamicBoundsCheckR2012b(outputLength, 1, ex, &g_emlrtBCI,
                                            (emlrtConstCTX)sp);
            }
            i1 = outputLength;
          }
          startSampIdx_size[0] = 1;
          loop_ub = i1 - i;
          startSampIdx_size[1] = loop_ub;
          st.site = &h_emlrtRSI;
          indexShapeCheck(&st, ex, startSampIdx_size);
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &j_emlrtBCI, (emlrtConstCTX)sp);
          }
          for (i1 = 0; i1 < loop_ub; i1++) {
            tmp_data[i1] = (int8_T)SD->f2.Ibdn_data[i + i1];
          }
          i = 256;
          emlrtSubAssignSizeCheckR2012b(&i, 1, &loop_ub, 1, &b_emlrtECI,
                                        (emlrtCTX)sp);
          memcpy(&I_reshaped_data[b_i * 256], &tmp_data[0],
                 256U * sizeof(int8_T));
          if (sampleIndex > outputLength) {
            i = 0;
            outputLength = 0;
          } else {
            if (sampleIndex > Qbdn_size) {
              emlrtDynamicBoundsCheckR2012b(sampleIndex, 1, Qbdn_size,
                                            &h_emlrtBCI, (emlrtConstCTX)sp);
            }
            i = sampleIndex - 1;
            if (outputLength > Qbdn_size) {
              emlrtDynamicBoundsCheckR2012b(outputLength, 1, Qbdn_size,
                                            &i_emlrtBCI, (emlrtConstCTX)sp);
            }
          }
          startSampIdx_size[0] = 1;
          loop_ub = outputLength - i;
          startSampIdx_size[1] = loop_ub;
          st.site = &g_emlrtRSI;
          indexShapeCheck(&st, Qbdn_size, startSampIdx_size);
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &l_emlrtBCI, (emlrtConstCTX)sp);
          }
          for (i1 = 0; i1 < loop_ub; i1++) {
            tmp_data[i1] = (int8_T)Qbdn_data[i + i1];
          }
          i = 256;
          emlrtSubAssignSizeCheckR2012b(&i, 1, &loop_ub, 1, &c_emlrtECI,
                                        (emlrtCTX)sp);
          memcpy(&Q_reshaped_data[b_i * 256], &tmp_data[0],
                 256U * sizeof(int8_T));
          if (*emlrtBreakCheckR2012bFlagVar != 0) {
            emlrtBreakCheckR2012b((emlrtConstCTX)sp);
          }
        }
        /*  Décision ML */
        for (b_i = 0; b_i < numSymbols_tmp; b_i++) {
          real_T I_reshaped[256];
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &k_emlrtBCI, (emlrtConstCTX)sp);
          }
          for (i = 0; i < 256; i++) {
            I_reshaped[i] = I_reshaped_data[i + 256 * b_i];
          }
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &u_emlrtBCI, (emlrtConstCTX)sp);
          }
          Ibits_data[b_i] = (b_sumColumnB(I_reshaped) > 128.0);
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &m_emlrtBCI, (emlrtConstCTX)sp);
          }
          for (i = 0; i < 256; i++) {
            I_reshaped[i] = Q_reshaped_data[i + 256 * b_i];
          }
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &x_emlrtBCI, (emlrtConstCTX)sp);
          }
          Qbits_data[b_i] = (b_sumColumnB(I_reshaped) > 128.0);
          if (*emlrtBreakCheckR2012bFlagVar != 0) {
            emlrtBreakCheckR2012b((emlrtConstCTX)sp);
          }
        }
        /*  Construction du message désétalé */
        loop_ub = numSymbols_tmp << 1;
        memset(&despreadMessage_data[0], 0,
               (uint32_T)loop_ub * sizeof(boolean_T));
        for (b_i = 0; b_i < numSymbols_tmp; b_i++) {
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &v_emlrtBCI, (emlrtConstCTX)sp);
          }
          i = (b_i + 1) << 1;
          if (i - 1 > loop_ub) {
            emlrtDynamicBoundsCheckR2012b(i - 1, 1, loop_ub, &w_emlrtBCI,
                                          (emlrtConstCTX)sp);
          }
          despreadMessage_data[i - 2] = Ibits_data[b_i];
          if (b_i + 1 > numSymbols_tmp) {
            emlrtDynamicBoundsCheckR2012b(b_i + 1, 1, numSymbols_tmp,
                                          &y_emlrtBCI, (emlrtConstCTX)sp);
          }
          if (i > loop_ub) {
            emlrtDynamicBoundsCheckR2012b(i, 1, loop_ub, &ab_emlrtBCI,
                                          (emlrtConstCTX)sp);
          }
          despreadMessage_data[i - 1] = Qbits_data[b_i];
          if (*emlrtBreakCheckR2012bFlagVar != 0) {
            emlrtBreakCheckR2012b((emlrtConstCTX)sp);
          }
        }
        /*  Correction d'erreurs BCH */
        if (loop_ub >= 51) {
          startSampIdx_size[0] = 1;
          sampleIndex = loop_ub - 50;
          startSampIdx_size[1] = loop_ub - 50;
          st.site = &f_emlrtRSI;
          indexShapeCheck(&st, loop_ub, startSampIdx_size);
          st.site = &f_emlrtRSI;
          memset(&rxPayload[0], 0, 202U * sizeof(boolean_T));
          if (loop_ub - 50 >= 202) {
            for (i = 0; i < 202; i++) {
              if (i + 1 > loop_ub - 50) {
                emlrtDynamicBoundsCheckR2012b(i + 1, 1, loop_ub - 50,
                                              &bb_emlrtBCI, &st);
              }
              rxPayload[i] = despreadMessage_data[i + 50];
            }
          } else {
            if (loop_ub - 50 > 202) {
              emlrtDynamicBoundsCheckR2012b(loop_ub - 50, 1, 202, &emlrtBCI,
                                            &st);
            }
            memcpy(&rxPayload[0], &despreadMessage_data[50],
                   (uint32_T)sampleIndex * sizeof(boolean_T));
          }
          *rxStatus = true;
        }
      }
    }
  }
  emxFree_boolean_T(sp, &x);
  emxFree_real_T(sp, &rxBits);
  emxFree_creal_T(sp, &syncedQPSK);
  emxFree_creal_T(sp, &coarseSyncOut);
  emxFree_creal_T(sp, &rxBurst);
  emlrtHeapReferenceStackLeaveFcnR2012b((emlrtConstCTX)sp);
  return errs;
}

/* End of code generation (dsss_receiver.c) */
