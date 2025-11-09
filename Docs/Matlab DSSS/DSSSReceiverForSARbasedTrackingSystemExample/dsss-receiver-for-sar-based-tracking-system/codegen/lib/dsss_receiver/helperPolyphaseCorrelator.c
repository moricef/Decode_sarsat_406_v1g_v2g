/*
 * File: helperPolyphaseCorrelator.c
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

/* Include Files */
#include "helperPolyphaseCorrelator.h"
#include "dsss_receiver_emxutil.h"
#include "dsss_receiver_rtwutil.h"
#include "dsss_receiver_types.h"
#include "rt_nonfinite.h"
#include "rt_nonfinite.h"
#include <math.h>

/* Function Definitions */
/*
 * HELPERPOLYPHASECORRELATOR Corrélation polyphase pour la détection de
 * préambule
 *
 * Arguments    : const emxArray_creal_T *rxBuffer
 *                const creal_T referenceSignal[175]
 *                double sps
 *                double idx_data[]
 *                int idx_size[2]
 *                emxArray_real_T *corrBuffer
 * Return Type  : void
 */
void helperPolyphaseCorrelator(const emxArray_creal_T *rxBuffer,
                               const creal_T referenceSignal[175], double sps,
                               double idx_data[], int idx_size[2],
                               emxArray_real_T *corrBuffer)
{
  emxArray_creal_T *A;
  emxArray_creal_T *C;
  emxArray_real_T *ex;
  emxArray_real_T *startIdxs;
  emxArray_real_T *xcorrBuffer;
  emxArray_real_T *y;
  creal_T B[175];
  const creal_T *rxBuffer_data;
  creal_T *A_data;
  creal_T *C_data;
  double B_re_tmp;
  double b_B_re_tmp;
  double bsum;
  double *corrBuffer_data;
  double *startIdxs_data;
  double *xcorrBuffer_data;
  int b_i;
  int b_k;
  int emptyDimValue;
  int i;
  int k;
  int last;
  int lastBlockLength;
  int loop_ub_tmp;
  int nA;
  int nblocks;
  boolean_T exitg1;
  boolean_T guard1;
  rxBuffer_data = rxBuffer->data;
  /*  Décimation du buffer d'entrée */
  if ((int)sps > 0) {
    if ((unsigned int)(int)sps == 0U) {
      emptyDimValue = MAX_int32_T;
    } else {
      emptyDimValue =
          (int)((unsigned int)rxBuffer->size[0] / (unsigned int)(int)sps);
    }
  } else {
    emptyDimValue = 0;
  }
  /*  Initialisation du buffer de corrélation */
  emxInit_real_T(&xcorrBuffer, 2);
  loop_ub_tmp = (int)(((double)emptyDimValue + 175.0) - 1.0);
  i = xcorrBuffer->size[0] * xcorrBuffer->size[1];
  xcorrBuffer->size[0] = (int)(((double)emptyDimValue + 175.0) - 1.0);
  last = (int)sps;
  xcorrBuffer->size[1] = (int)sps;
  emxEnsureCapacity_real_T(xcorrBuffer, i);
  xcorrBuffer_data = xcorrBuffer->data;
  b_i = (int)(((double)emptyDimValue + 175.0) - 1.0) * (int)sps;
  for (i = 0; i < b_i; i++) {
    xcorrBuffer_data[i] = 0.0;
  }
  /*  Initialisation des indices de détection */
  emxInit_real_T(&startIdxs, 2);
  i = startIdxs->size[0] * startIdxs->size[1];
  startIdxs->size[0] = 1;
  startIdxs->size[1] = (int)sps;
  emxEnsureCapacity_real_T(startIdxs, i);
  startIdxs_data = startIdxs->data;
  /*  Corrélation pour chaque phase */
  emxInit_creal_T(&C);
  if ((int)sps - 1 >= 0) {
    nblocks = (int)sps;
    lastBlockLength = emptyDimValue;
    for (b_i = 0; b_i < 175; b_i++) {
      B[b_i].re = referenceSignal[b_i].re;
      B[b_i].im = -referenceSignal[b_i].im;
    }
  }
  emxInit_creal_T(&A);
  emxInit_real_T(&y, 1);
  for (k = 0; k < last; k++) {
    /*  Corrélation entre le signal décimé et le préambule */
    i = A->size[0];
    A->size[0] = emptyDimValue;
    emxEnsureCapacity_creal_T(A, i);
    A_data = A->data;
    for (i = 0; i < lastBlockLength; i++) {
      nA = k + nblocks * i;
      A_data[i].re = rxBuffer_data[nA].re;
      A_data[i].im = -rxBuffer_data[nA].im;
    }
    nA = A->size[0] - 1;
    if (A->size[0] == 0) {
      b_i = 174;
    } else {
      b_i = A->size[0] + 173;
    }
    i = C->size[0];
    C->size[0] = b_i + 1;
    emxEnsureCapacity_creal_T(C, i);
    C_data = C->data;
    for (i = 0; i <= b_i; i++) {
      C_data[i].re = 0.0;
      C_data[i].im = 0.0;
    }
    if (A->size[0] > 0) {
      if (A->size[0] < 175) {
        for (b_i = 0; b_i <= nA; b_i++) {
          for (b_k = 0; b_k < 175; b_k++) {
            double c_B_re_tmp;
            bsum = A_data[b_i].re;
            b_B_re_tmp = B[b_k].im;
            B_re_tmp = A_data[b_i].im;
            c_B_re_tmp = B[b_k].re;
            i = b_i + b_k;
            C_data[i].re += bsum * c_B_re_tmp - B_re_tmp * b_B_re_tmp;
            C_data[i].im += bsum * b_B_re_tmp + B_re_tmp * c_B_re_tmp;
          }
        }
      } else {
        for (b_i = 0; b_i < 175; b_i++) {
          b_B_re_tmp = B[b_i].re;
          bsum = B[b_i].im;
          for (b_k = 0; b_k <= nA; b_k++) {
            double c_B_re_tmp;
            B_re_tmp = A_data[b_k].im;
            c_B_re_tmp = A_data[b_k].re;
            i = b_i + b_k;
            C_data[i].re += b_B_re_tmp * c_B_re_tmp - bsum * B_re_tmp;
            C_data[i].im += b_B_re_tmp * B_re_tmp + bsum * c_B_re_tmp;
          }
        }
      }
    }
    nA = C->size[0];
    i = y->size[0];
    y->size[0] = C->size[0];
    emxEnsureCapacity_real_T(y, i);
    corrBuffer_data = y->data;
    for (b_i = 0; b_i < nA; b_i++) {
      corrBuffer_data[b_i] = rt_hypotd_snf(C_data[b_i].re, C_data[b_i].im);
    }
    for (i = 0; i < loop_ub_tmp; i++) {
      xcorrBuffer_data[i + xcorrBuffer->size[0] * k] = corrBuffer_data[i];
    }
    /*  Détection du pic de corrélation */
    if (!rtIsNaN(xcorrBuffer_data[xcorrBuffer->size[0] * k])) {
      b_k = 1;
    } else {
      b_k = 0;
      b_i = 2;
      exitg1 = false;
      while ((!exitg1) &&
             (b_i <= (int)(((double)emptyDimValue + 175.0) - 1.0))) {
        if (!rtIsNaN(xcorrBuffer_data[(b_i + xcorrBuffer->size[0] * k) - 1])) {
          b_k = b_i;
          exitg1 = true;
        } else {
          b_i++;
        }
      }
    }
    if (b_k == 0) {
      B_re_tmp = xcorrBuffer_data[xcorrBuffer->size[0] * k];
      b_k = 1;
    } else {
      B_re_tmp = xcorrBuffer_data[(b_k + xcorrBuffer->size[0] * k) - 1];
      i = b_k + 1;
      for (b_i = i; b_i <= loop_ub_tmp; b_i++) {
        b_B_re_tmp = xcorrBuffer_data[(b_i + xcorrBuffer->size[0] * k) - 1];
        if (B_re_tmp < b_B_re_tmp) {
          B_re_tmp = b_B_re_tmp;
          b_k = b_i;
        }
      }
    }
    if (xcorrBuffer_data[(b_k + xcorrBuffer->size[0] * k) - 1] >
        0.35 * B_re_tmp) {
      startIdxs_data[k] = ((double)b_k - 200.0) + 1.0;
    } else {
      startIdxs_data[k] = 0.0;
    }
  }
  emxFree_creal_T(&A);
  emxFree_creal_T(&C);
  /*  Trouver le pic de corrélation maximal parmi toutes les phases */
  emxInit_real_T(&ex, 2);
  i = ex->size[0] * ex->size[1];
  ex->size[0] = 1;
  ex->size[1] = (int)sps;
  emxEnsureCapacity_real_T(ex, i);
  corrBuffer_data = ex->data;
  if (xcorrBuffer->size[1] >= 1) {
    for (nblocks = 0; nblocks < last; nblocks++) {
      corrBuffer_data[nblocks] =
          xcorrBuffer_data[xcorrBuffer->size[0] * nblocks];
      for (b_i = 2; b_i <= loop_ub_tmp; b_i++) {
        boolean_T p;
        b_B_re_tmp = corrBuffer_data[nblocks];
        bsum = xcorrBuffer_data[(b_i + xcorrBuffer->size[0] * nblocks) - 1];
        if (rtIsNaN(bsum)) {
          p = false;
        } else if (rtIsNaN(b_B_re_tmp)) {
          p = true;
        } else {
          p = (b_B_re_tmp < bsum);
        }
        if (p) {
          corrBuffer_data[nblocks] = bsum;
        }
      }
    }
  }
  if (ex->size[1] <= 2) {
    if (ex->size[1] == 1) {
      B_re_tmp = corrBuffer_data[0];
      b_k = 1;
    } else {
      B_re_tmp = corrBuffer_data[ex->size[1] - 1];
      if ((corrBuffer_data[0] < B_re_tmp) ||
          (rtIsNaN(corrBuffer_data[0]) && (!rtIsNaN(B_re_tmp)))) {
        b_k = (int)sps;
      } else {
        B_re_tmp = corrBuffer_data[0];
        b_k = 1;
      }
    }
  } else {
    if (!rtIsNaN(corrBuffer_data[0])) {
      b_k = 1;
    } else {
      b_k = 0;
      k = 2;
      exitg1 = false;
      while ((!exitg1) && (k <= (int)sps)) {
        if (!rtIsNaN(corrBuffer_data[k - 1])) {
          b_k = k;
          exitg1 = true;
        } else {
          k++;
        }
      }
    }
    if (b_k == 0) {
      B_re_tmp = corrBuffer_data[0];
      b_k = 1;
    } else {
      B_re_tmp = corrBuffer_data[b_k - 1];
      i = b_k + 1;
      for (k = i; k <= last; k++) {
        b_B_re_tmp = corrBuffer_data[k - 1];
        if (B_re_tmp < b_B_re_tmp) {
          B_re_tmp = b_B_re_tmp;
          b_k = k;
        }
      }
    }
  }
  emxFree_real_T(&ex);
  /*  Extraction du buffer de corrélation pour la phase sélectionnée */
  if (xcorrBuffer->size[0] < 175) {
    i = 0;
    nA = 0;
  } else {
    i = 174;
    nA = (int)(((double)emptyDimValue + 175.0) - 1.0);
  }
  b_i = nA - i;
  nA = corrBuffer->size[0];
  corrBuffer->size[0] = b_i;
  emxEnsureCapacity_real_T(corrBuffer, nA);
  corrBuffer_data = corrBuffer->data;
  for (nA = 0; nA < b_i; nA++) {
    corrBuffer_data[nA] =
        xcorrBuffer_data[(i + nA) + xcorrBuffer->size[0] * (b_k - 1)];
  }
  /*  Décision de détection finale */
  guard1 = false;
  if (b_i == 0) {
    guard1 = true;
  } else {
    nA = y->size[0];
    y->size[0] = b_i;
    emxEnsureCapacity_real_T(y, nA);
    corrBuffer_data = y->data;
    for (k = 0; k < b_i; k++) {
      corrBuffer_data[k] =
          xcorrBuffer_data[(i + k) + xcorrBuffer->size[0] * (b_k - 1)];
    }
    if (y->size[0] <= 1024) {
      lastBlockLength = 0;
      nblocks = 1;
    } else {
      b_i = 1024;
      nblocks = (int)((unsigned int)y->size[0] >> 10);
      lastBlockLength = y->size[0] - (nblocks << 10);
      if (lastBlockLength > 0) {
        nblocks++;
      } else {
        lastBlockLength = 1024;
      }
    }
    b_B_re_tmp = corrBuffer_data[0];
    for (k = 2; k <= b_i; k++) {
      b_B_re_tmp += corrBuffer_data[k - 1];
    }
    for (emptyDimValue = 2; emptyDimValue <= nblocks; emptyDimValue++) {
      b_i = (emptyDimValue - 1) << 10;
      bsum = corrBuffer_data[b_i];
      if (emptyDimValue == nblocks) {
        nA = lastBlockLength;
      } else {
        nA = 1024;
      }
      for (k = 2; k <= nA; k++) {
        bsum += corrBuffer_data[(b_i + k) - 1];
      }
      b_B_re_tmp += bsum;
    }
    if (B_re_tmp < 5.5 * (b_B_re_tmp / (double)y->size[0])) {
      guard1 = true;
    } else {
      b_B_re_tmp = startIdxs_data[b_k - 1];
      if (b_B_re_tmp == 0.0) {
        idx_size[0] = 0;
        idx_size[1] = 0;
      } else {
        idx_size[0] = 1;
        idx_size[1] = 1;
        idx_data[0] = fmax(1.0, ((b_B_re_tmp - 1.0) * sps + (double)b_k) -
                                    floor(sps / 2.0));
      }
    }
  }
  if (guard1) {
    idx_size[0] = 0;
    idx_size[1] = 0;
  }
  emxFree_real_T(&y);
  emxFree_real_T(&startIdxs);
  emxFree_real_T(&xcorrBuffer);
}

/*
 * File trailer for helperPolyphaseCorrelator.c
 *
 * [EOF]
 */
