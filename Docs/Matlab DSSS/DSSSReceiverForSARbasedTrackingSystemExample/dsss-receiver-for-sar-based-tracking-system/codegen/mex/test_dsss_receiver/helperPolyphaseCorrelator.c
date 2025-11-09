/*
 * helperPolyphaseCorrelator.c
 *
 * Code generation for function 'helperPolyphaseCorrelator'
 *
 */

/* Include files */
#include "helperPolyphaseCorrelator.h"
#include "conv.h"
#include "rt_nonfinite.h"
#include "sumMatrixIncludeNaN.h"
#include "test_dsss_receiver_data.h"
#include "test_dsss_receiver_types.h"
#include "mwmathutil.h"
#include <string.h>

/* Function Definitions */
void helperPolyphaseCorrelator(test_dsss_receiverStackData *SD,
                               const emlrtStack *sp,
                               const creal_T rxBuffer[307200],
                               const creal_T referenceSignal[175],
                               real_T idx_data[], int32_T idx_size[2],
                               real_T corrBuffer[38400])
{
  creal_T b_referenceSignal[175];
  real_T ex[8];
  real_T b_ex;
  real_T d;
  real_T s;
  int32_T startIdxs[8];
  int32_T b_k;
  int32_T i;
  int32_T idx;
  int32_T k;
  boolean_T exitg1;
  /*  HELPERPOLYPHASECORRELATOR Corrélation polyphase pour la détection de
   * préambule */
  /*  Décimation du buffer d'entrée */
  /*  Initialisation du buffer de corrélation */
  memset(&SD->u1.f0.xcorrBuffer[0], 0, 308592U * sizeof(real_T));
  /*  Initialisation des indices de détection */
  /*  Corrélation pour chaque phase */
  for (i = 0; i < 175; i++) {
    b_referenceSignal[i].re = referenceSignal[i].re;
    b_referenceSignal[i].im = -referenceSignal[i].im;
  }
  for (k = 0; k < 8; k++) {
    /*  Corrélation entre le signal décimé et le préambule */
    for (i = 0; i < 38400; i++) {
      idx = k + (i << 3);
      SD->u1.f0.rxBuffer[i].re = rxBuffer[idx].re;
      SD->u1.f0.rxBuffer[i].im = -rxBuffer[idx].im;
    }
    conv(SD->u1.f0.rxBuffer, b_referenceSignal, SD->u1.f0.x);
    for (b_k = 0; b_k < 38574; b_k++) {
      SD->u1.f0.xcorrBuffer[b_k + 38574 * k] =
          muDoubleScalarHypot(SD->u1.f0.x[b_k].re, SD->u1.f0.x[b_k].im);
    }
    /*  Détection du pic de corrélation */
    b_ex = SD->u1.f0.xcorrBuffer[38574 * k];
    if (!muDoubleScalarIsNaN(b_ex)) {
      idx = 1;
    } else {
      idx = 0;
      b_k = 2;
      exitg1 = false;
      while ((!exitg1) && (b_k < 38575)) {
        if (!muDoubleScalarIsNaN(
                SD->u1.f0.xcorrBuffer[(b_k + 38574 * k) - 1])) {
          idx = b_k;
          exitg1 = true;
        } else {
          b_k++;
        }
      }
    }
    if (idx == 0) {
      idx = 1;
    } else {
      b_ex = SD->u1.f0.xcorrBuffer[(idx + 38574 * k) - 1];
      i = idx + 1;
      for (b_k = i; b_k < 38575; b_k++) {
        d = SD->u1.f0.xcorrBuffer[(b_k + 38574 * k) - 1];
        if (b_ex < d) {
          b_ex = d;
          idx = b_k;
        }
      }
    }
    if (SD->u1.f0.xcorrBuffer[(idx + 38574 * k) - 1] > 0.35 * b_ex) {
      startIdxs[k] = idx - 199;
    } else {
      startIdxs[k] = 0;
    }
    if (*emlrtBreakCheckR2012bFlagVar != 0) {
      emlrtBreakCheckR2012b((emlrtConstCTX)sp);
    }
  }
  /*  Trouver le pic de corrélation maximal parmi toutes les phases */
  for (idx = 0; idx < 8; idx++) {
    ex[idx] = SD->u1.f0.xcorrBuffer[38574 * idx];
    for (b_k = 0; b_k < 38573; b_k++) {
      boolean_T p;
      d = SD->u1.f0.xcorrBuffer[(b_k + 38574 * idx) + 1];
      if (muDoubleScalarIsNaN(d)) {
        p = false;
      } else {
        s = ex[idx];
        if (muDoubleScalarIsNaN(s)) {
          p = true;
        } else {
          p = (s < d);
        }
      }
      if (p) {
        ex[idx] = d;
      }
    }
  }
  if (!muDoubleScalarIsNaN(ex[0])) {
    idx = 1;
  } else {
    idx = 0;
    k = 2;
    exitg1 = false;
    while ((!exitg1) && (k < 9)) {
      if (!muDoubleScalarIsNaN(ex[k - 1])) {
        idx = k;
        exitg1 = true;
      } else {
        k++;
      }
    }
  }
  if (idx == 0) {
    b_ex = ex[0];
    b_k = 0;
  } else {
    b_ex = ex[idx - 1];
    b_k = idx - 1;
    i = idx + 1;
    for (k = i; k < 9; k++) {
      d = ex[k - 1];
      if (b_ex < d) {
        b_ex = d;
        b_k = k - 1;
      }
    }
  }
  /*  Extraction du buffer de corrélation pour la phase sélectionnée */
  /*  Décision de détection finale */
  for (k = 0; k < 38400; k++) {
    d = SD->u1.f0.xcorrBuffer[(k + 38574 * b_k) + 174];
    corrBuffer[k] = d;
    SD->u1.f0.y[k] = d;
  }
  s = sumColumnB4(SD->u1.f0.y);
  for (idx = 0; idx < 8; idx++) {
    s += b_sumColumnB4(SD->u1.f0.y, ((idx + 1) << 12) + 1);
  }
  if (b_ex < 5.5 * ((s + sumColumnB(SD->u1.f0.y)) / 38400.0)) {
    idx_size[0] = 0;
    idx_size[1] = 0;
  } else if (startIdxs[b_k] == 0) {
    idx_size[0] = 0;
    idx_size[1] = 0;
  } else {
    idx_size[0] = 1;
    idx_size[1] = 1;
    idx_data[0] = muDoubleScalarMax(
        1.0, ((real_T)(((startIdxs[b_k] - 1) << 3) + b_k) + 1.0) - 4.0);
  }
}

/* End of code generation (helperPolyphaseCorrelator.c) */
