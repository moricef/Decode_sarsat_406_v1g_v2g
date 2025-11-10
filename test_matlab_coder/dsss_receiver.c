/*
 * File: dsss_receiver.c
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

/* Include Files */
#include "dsss_receiver.h"
#include "dsss_receiver_emxutil.h"
#include "dsss_receiver_rtwutil.h"
#include "dsss_receiver_types.h"
#include "helperPolyphaseCorrelator.h"
#include "minOrMax.h"
#include "pskdemod.h"
#include "rt_nonfinite.h"
#include "sign.h"
#include "rt_nonfinite.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Function Declarations */
static void carrierSynchronizer(const emxArray_creal_T *input,
                                emxArray_creal_T *output);

static void simpleAGC(const emxArray_creal_T *input, double sps,
                      emxArray_creal_T *output);

/* Function Definitions */
/*
 * Arguments    : const emxArray_creal_T *input
 *                emxArray_creal_T *output
 * Return Type  : void
 */
static void carrierSynchronizer(const emxArray_creal_T *input,
                                emxArray_creal_T *output)
{
  const creal_T *input_data;
  creal_T *output_data;
  double freq;
  double phase;
  int i;
  int loop_ub;
  input_data = input->data;
  loop_ub = input->size[0];
  i = output->size[0];
  output->size[0] = input->size[0];
  emxEnsureCapacity_creal_T(output, i);
  output_data = output->data;
  phase = 0.0;
  freq = 0.0;
  for (i = 0; i < loop_ub; i++) {
    double b_error;
    double b_re_tmp;
    double re;
    double re_tmp;
    double y_im;
    if (phase * 0.0 == 0.0) {
      b_error = cos(-phase);
      y_im = sin(-phase);
    } else if (-phase == 0.0) {
      b_error = rtNaN;
      y_im = 0.0;
    } else {
      b_error = rtNaN;
      y_im = rtNaN;
    }
    re_tmp = input_data[i].re;
    b_re_tmp = input_data[i].im;
    re = re_tmp * b_error - b_re_tmp * y_im;
    b_error = re_tmp * y_im + b_re_tmp * b_error;
    output_data[i].re = re;
    output_data[i].im = b_error;
    if (i + 1 > 1) {
      y_im = output_data[i - 1].re;
      re_tmp = -output_data[i - 1].im;
      b_error = rt_atan2d_snf(re * re_tmp + b_error * y_im,
                              re * y_im - b_error * re_tmp) /
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
  }
}

/*
 * Arguments    : const emxArray_creal_T *input
 *                double sps
 *                emxArray_creal_T *output
 * Return Type  : void
 */
static void simpleAGC(const emxArray_creal_T *input, double sps,
                      emxArray_creal_T *output)
{
  const creal_T *input_data;
  creal_T *output_data;
  double avgPower;
  int i;
  int loop_ub;
  input_data = input->data;
  loop_ub = input->size[0];
  i = output->size[0];
  output->size[0] = input->size[0];
  emxEnsureCapacity_creal_T(output, i);
  output_data = output->data;
  avgPower = 0.0;
  for (i = 0; i < loop_ub; i++) {
    double y;
    if (i + 1 == 1) {
      y = rt_hypotd_snf(input_data[0].re, input_data[0].im);
      avgPower = y * y;
    } else {
      double r;
      y = 10.0 * sps;
      r = (double)i + 1.0;
      if (!(y == 0.0)) {
        if (rtIsNaN(y)) {
          r = rtNaN;
        } else if (rtIsInf(y)) {
          if (y < 0.0) {
            r = y;
          }
        } else {
          boolean_T rEQ0;
          r = fmod((double)i + 1.0, y);
          rEQ0 = (r == 0.0);
          if ((!rEQ0) && (y > floor(y))) {
            double q;
            q = fabs(((double)i + 1.0) / y);
            rEQ0 = !(fabs(q - floor(q + 0.5)) > 2.2204460492503131E-16 * q);
          }
          if (rEQ0) {
            r = y * 0.0;
          } else if (y < 0.0) {
            r += y;
          }
        }
      }
      if (r == 1.0) {
        y = rt_hypotd_snf(input_data[i].re, input_data[i].im);
        avgPower = 0.9 * avgPower + 0.1 * (y * y);
      }
    }
    if (avgPower > 0.0) {
      y = 1.0 / sqrt(avgPower);
    } else {
      y = 1.0;
    }
    output_data[i].re = y * input_data[i].re;
    output_data[i].im = y * input_data[i].im;
  }
}

/*
 * DSSS_RECEIVER Implémente un récepteur DSSS COSPAS-SARSAT
 *
 * Arguments    : const creal_T otaBuffer[307200]
 *                const struct0_T *settings
 *                boolean_T rxPayload[202]
 *                int *errs
 *                boolean_T *rxStatus
 * Return Type  : void
 */
void dsss_receiver(const creal_T otaBuffer[307200], const struct0_T *settings,
                   boolean_T rxPayload[202], int *errs, boolean_T *rxStatus)
{
  static const creal_T b_const[4] = {{
                                         0.70710678118654757, /* re */
                                         0.70710678118654746  /* im */
                                     },
                                     {
                                         -0.70710678118654746, /* re */
                                         0.70710678118654757   /* im */
                                     },
                                     {
                                         -0.70710678118654768, /* re */
                                         -0.70710678118654746  /* im */
                                     },
                                     {
                                         0.70710678118654735, /* re */
                                         -0.70710678118654768 /* im */
                                     }};
  static creal_T preambleQPSK[3200];
  static const signed char iv[23] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  static const signed char iv1[23] = {0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0,
                                      0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0};
  static const signed char mapping[4] = {0, 1, 3, 2};
  static boolean_T PRN_I[38400];
  static boolean_T PRN_Q[38400];
  static boolean_T rxCi_data[38400];
  static boolean_T rxCq_data[38400];
  emxArray_boolean_T *x;
  emxArray_creal_T *rxAGCSamples;
  emxArray_creal_T *sampleBuffer;
  emxArray_creal_T *sampleBufferQPSK;
  emxArray_real_T *imagPart;
  emxArray_real_T *realPart;
  creal_T *rxAGCSamples_data;
  creal_T *sampleBufferQPSK_data;
  creal_T *sampleBuffer_data;
  double d;
  double minLen;
  double numBurstSamples_tmp;
  double sps;
  double startSampIdx_data;
  double *imagPart_data;
  double *realPart_data;
  int startSampIdx_size[2];
  int b_i;
  int i;
  int i1;
  int k;
  int maxBits;
  int numSymbols;
  signed char I_reshaped_data[38400];
  signed char Q_reshaped_data[38400];
  signed char iloc[3200];
  signed char iv2[3200];
  signed char b_reg[23];
  signed char reg[23];
  boolean_T despreadMessage_data[300];
  boolean_T exitg1;
  boolean_T *x_data;
  /*  Constantes */
  /*  m/s */
  /*  Paramètres système */
  /*  MHz */
  /*  bits */
  /*  chips/s */
  /*  facteur d'étalement */
  /*  Calculs dérivés */
  sps = 2.0 * settings->oversampling;
  numBurstSamples_tmp = 38400.0 * sps;
  /*  Initialisation des séquences PRN */
  /*  Génération des séquences PRN avec fonctions codegen-compatibles */
  for (i = 0; i < 23; i++) {
    reg[i] = iv[i];
  }
  /*  Fonctions codegen-compatibles (inchangées) */
  for (b_i = 0; b_i < 38400; b_i++) {
    PRN_I[b_i] = (reg[22] != 0);
    /*  Polynomial: X^23 + X^18 + 1 */
    b_reg[0] = (signed char)((reg[22] != 0) != (reg[17] != 0));
    for (i = 0; i < 22; i++) {
      b_reg[i + 1] = reg[i];
    }
    for (i = 0; i < 23; i++) {
      reg[i] = b_reg[i];
    }
  }
  for (i = 0; i < 23; i++) {
    reg[i] = iv1[i];
  }
  /*  Fonctions codegen-compatibles (inchangées) */
  for (b_i = 0; b_i < 38400; b_i++) {
    PRN_Q[b_i] = (reg[22] != 0);
    /*  Polynomial: X^23 + X^18 + 1 */
    b_reg[0] = (signed char)((reg[22] != 0) != (reg[17] != 0));
    for (i = 0; i < 22; i++) {
      b_reg[i + 1] = reg[i];
    }
    for (i = 0; i < 23; i++) {
      reg[i] = b_reg[i];
    }
  }
  /*  Génération du préambule QPSK */
  for (maxBits = 0; maxBits < 3200; maxBits++) {
    iv2[maxBits] = (signed char)(2 * PRN_I[maxBits] + PRN_Q[maxBits]);
    iloc[maxBits] = 0;
    k = 0;
    exitg1 = false;
    while ((!exitg1) && (k < 4)) {
      if (iv2[maxBits] == mapping[k]) {
        iloc[maxBits] = (signed char)(k + 1);
        exitg1 = true;
      } else {
        k++;
      }
    }
    preambleQPSK[maxBits] = b_const[iloc[maxBits] - 1];
  }
  /*  Buffer d'échantillons */
  emxInit_creal_T(&sampleBuffer);
  k = (int)(numBurstSamples_tmp * 2.0);
  i = sampleBuffer->size[0];
  sampleBuffer->size[0] = k;
  emxEnsureCapacity_creal_T(sampleBuffer, i);
  sampleBuffer_data = sampleBuffer->data;
  for (i = 0; i < k; i++) {
    sampleBuffer_data[i].re = 0.0;
    sampleBuffer_data[i].im = 0.0;
  }
  if (numBurstSamples_tmp <= 307200.0) {
    if (numBurstSamples_tmp < 1.0) {
      k = 0;
    } else {
      k = (int)numBurstSamples_tmp;
    }
    for (i = 0; i < k; i++) {
      sampleBuffer_data[i] = otaBuffer[i];
    }
  } else {
    for (i = 0; i < 307200; i++) {
      sampleBuffer_data[i] = otaBuffer[i];
    }
  }
  /*  AGC codegen-compatible */
  emxInit_creal_T(&rxAGCSamples);
  simpleAGC(sampleBuffer, sps, rxAGCSamples);
  rxAGCSamples_data = rxAGCSamples->data;
  /*  Conversion explicite en double et saturation */
  numSymbols = rxAGCSamples->size[0];
  emxInit_real_T(&realPart, 1);
  i = realPart->size[0];
  realPart->size[0] = rxAGCSamples->size[0];
  emxEnsureCapacity_real_T(realPart, i);
  realPart_data = realPart->data;
  for (k = 0; k < numSymbols; k++) {
    realPart_data[k] =
        rt_hypotd_snf(rxAGCSamples_data[k].re, rxAGCSamples_data[k].im);
  }
  k = realPart->size[0] - 1;
  maxBits = 0;
  for (b_i = 0; b_i <= k; b_i++) {
    if (realPart_data[b_i] > 1.2) {
      maxBits++;
    }
  }
  emxInit_creal_T(&sampleBufferQPSK);
  i = sampleBufferQPSK->size[0];
  sampleBufferQPSK->size[0] = maxBits;
  emxEnsureCapacity_creal_T(sampleBufferQPSK, i);
  sampleBufferQPSK_data = sampleBufferQPSK->data;
  maxBits = 0;
  for (b_i = 0; b_i <= k; b_i++) {
    if (realPart_data[b_i] > 1.2) {
      sampleBufferQPSK_data[maxBits] = rxAGCSamples_data[b_i];
      maxBits++;
    }
  }
  b_sign(sampleBufferQPSK);
  sampleBufferQPSK_data = sampleBufferQPSK->data;
  maxBits = 0;
  for (b_i = 0; b_i <= k; b_i++) {
    if (realPart_data[b_i] > 1.2) {
      rxAGCSamples_data[b_i].re = 1.2 * sampleBufferQPSK_data[maxBits].re;
      rxAGCSamples_data[b_i].im = 1.2 * sampleBufferQPSK_data[maxBits].im;
      maxBits++;
    }
  }
  /*  Détection du préambule */
  /*  Préparation du buffer QPSK avec taille fixe */
  i = sampleBufferQPSK->size[0];
  sampleBufferQPSK->size[0] = rxAGCSamples->size[0];
  emxEnsureCapacity_creal_T(sampleBufferQPSK, i);
  sampleBufferQPSK_data = sampleBufferQPSK->data;
  for (i = 0; i < numSymbols; i++) {
    sampleBufferQPSK_data[i].re = 0.0;
    sampleBufferQPSK_data[i].im = 0.0;
  }
  if (rxAGCSamples->size[0] > sps / 2.0) {
    d = (double)rxAGCSamples->size[0] - sps / 2.0;
    if (d < 1.0) {
      k = 0;
    } else {
      k = (int)d;
    }
    i = realPart->size[0];
    realPart->size[0] = k;
    emxEnsureCapacity_real_T(realPart, i);
    realPart_data = realPart->data;
    for (i = 0; i < k; i++) {
      realPart_data[i] = rxAGCSamples_data[i].re;
    }
    d = sps / 2.0 + 1.0;
    if (d > rxAGCSamples->size[0]) {
      i = 0;
      i1 = 0;
    } else {
      i = (int)d - 1;
      i1 = rxAGCSamples->size[0];
    }
    emxInit_real_T(&imagPart, 1);
    k = i1 - i;
    i1 = imagPart->size[0];
    imagPart->size[0] = k;
    emxEnsureCapacity_real_T(imagPart, i1);
    imagPart_data = imagPart->data;
    for (i1 = 0; i1 < k; i1++) {
      imagPart_data[i1] = rxAGCSamples_data[i + i1].im;
    }
    minLen = fmin(realPart->size[0], imagPart->size[0]);
    if (minLen < 1.0) {
      k = 0;
    } else {
      k = (int)minLen;
    }
    for (i = 0; i < k; i++) {
      sampleBufferQPSK_data[i].re = realPart_data[i] + 0.0 * imagPart_data[i];
      sampleBufferQPSK_data[i].im = imagPart_data[i];
    }
    emxFree_real_T(&imagPart);
  }
  d = fmin(numBurstSamples_tmp, sampleBufferQPSK->size[0]);
  i = sampleBufferQPSK->size[0];
  if (d < 1.0) {
    sampleBufferQPSK->size[0] = 0;
  } else {
    sampleBufferQPSK->size[0] = (int)d;
  }
  emxEnsureCapacity_creal_T(sampleBufferQPSK, i);
  helperPolyphaseCorrelator(sampleBufferQPSK, &preambleQPSK[200], sps,
                            (double *)&startSampIdx_data, startSampIdx_size,
                            realPart);
  /*  Extraction du burst */
  memset(&rxPayload[0], 0, 202U * sizeof(boolean_T));
  *errs = 0;
  *rxStatus = false;
  emxInit_boolean_T(&x);
  if ((startSampIdx_size[0] != 0) && (startSampIdx_size[1] != 0)) {
    double startIdx_data;
    /*  Vérification que startSampIdx est un scalaire */
    /*  Calcul des indices avec vérification des limites */
    startIdx_data = startSampIdx_data;
    /*  Conversion explicite en double */
    maxBits = sampleBuffer->size[0];
    startSampIdx_data =
        fmin((startSampIdx_data + 38420.0 * sps) - 1.0, maxBits);
    /*  Vérification que les indices sont valides (version corrigée) */
    /*  Extraction des valeurs scalaires */
    /*  Conditions de validation */
    if ((!(startIdx_data > startSampIdx_data)) &&
        (!(startIdx_data > sampleBuffer->size[0]))) {
      /*  Utilisation des indices scalaires pour l'extraction */
      /*  Extraction sécurisée du burst avec pré-allocation */
      startSampIdx_data = (startSampIdx_data - startIdx_data) + 1.0;
      k = (int)startSampIdx_data;
      i = sampleBufferQPSK->size[0];
      sampleBufferQPSK->size[0] = (int)startSampIdx_data;
      emxEnsureCapacity_creal_T(sampleBufferQPSK, i);
      sampleBufferQPSK_data = sampleBufferQPSK->data;
      for (i = 0; i < k; i++) {
        sampleBufferQPSK_data[i].re = 0.0;
        sampleBufferQPSK_data[i].im = 0.0;
      }
      /*  Copie manuelle des échantillons */
      for (b_i = 0; b_i < k; b_i++) {
        minLen = (startIdx_data + ((double)b_i + 1.0)) - 1.0;
        if (minLen <= sampleBuffer->size[0]) {
          sampleBufferQPSK_data[b_i] = sampleBuffer_data[(int)minLen - 1];
        }
      }
      /*  Vérification que rxBurst n'est pas vide */
      if (sampleBufferQPSK->size[0] != 0) {
        boolean_T y;
        i = x->size[0];
        x->size[0] = (int)startSampIdx_data;
        emxEnsureCapacity_boolean_T(x, i);
        x_data = x->data;
        for (i = 0; i < k; i++) {
          x_data[i] = ((sampleBufferQPSK_data[i].re == 0.0) &&
                       (sampleBufferQPSK_data[i].im == 0.0));
        }
        y = true;
        maxBits = 1;
        exitg1 = false;
        while ((!exitg1) && (maxBits <= x->size[0])) {
          if (!x_data[maxBits - 1]) {
            y = false;
            exitg1 = true;
          } else {
            maxBits++;
          }
        }
        if (!y) {
          double rxCi[4];
          boolean_T Ibdn_data[38400];
          boolean_T Qbdn_data[38400];
          /*  Correction de fréquence et phase */
          i = rxAGCSamples->size[0];
          rxAGCSamples->size[0] = (int)startSampIdx_data;
          emxEnsureCapacity_creal_T(rxAGCSamples, i);
          rxAGCSamples_data = rxAGCSamples->data;
          minLen = 0.0;
          for (b_i = 0; b_i < k; b_i++) {
            double d1;
            if (minLen * 0.0 == 0.0) {
              startSampIdx_data = cos(-minLen);
              startIdx_data = sin(-minLen);
            } else if (-minLen == 0.0) {
              startSampIdx_data = rtNaN;
              startIdx_data = 0.0;
            } else {
              startSampIdx_data = rtNaN;
              startIdx_data = rtNaN;
            }
            d = sampleBufferQPSK_data[b_i].re;
            d1 = sampleBufferQPSK_data[b_i].im;
            rxAGCSamples_data[b_i].re =
                d * startSampIdx_data - d1 * startIdx_data;
            rxAGCSamples_data[b_i].im =
                d * startIdx_data + d1 * startSampIdx_data;
            minLen += 0.0062831853071795866 / numBurstSamples_tmp;
            if (minLen > 6.2831853071795862) {
              minLen -= 6.2831853071795862;
            }
          }
          carrierSynchronizer(rxAGCSamples, sampleBuffer);
          sampleBuffer_data = sampleBuffer->data;
          k = (int)floor((double)sampleBuffer->size[0] / sps);
          i = sampleBufferQPSK->size[0];
          sampleBufferQPSK->size[0] =
              (int)floor((double)sampleBuffer->size[0] / sps);
          emxEnsureCapacity_creal_T(sampleBufferQPSK, i);
          sampleBufferQPSK_data = sampleBufferQPSK->data;
          for (i = 0; i < k; i++) {
            sampleBufferQPSK_data[i].re = 0.0;
            sampleBufferQPSK_data[i].im = 0.0;
          }
          for (b_i = 0; b_i < k; b_i++) {
            minLen = (((double)b_i + 1.0) - 1.0) * sps + floor(sps / 2.0);
            if (minLen <= sampleBuffer->size[0]) {
              sampleBufferQPSK_data[b_i] = sampleBuffer_data[(int)minLen - 1];
            }
          }
          /*  Démodulation et désétalement */
          if (sampleBufferQPSK->size[0] > 1) {
            pskdemod(sampleBufferQPSK, realPart);
            realPart_data = realPart->data;
            /*  Extraction des bits I et Q avec vérification de taille */
            /*  Pré-allocation des vecteurs I et Q */
            maxBits = (int)fmin(realPart->size[0], 76800.0);
            k = (int)ceil((double)maxBits / 2.0);
            if (k - 1 >= 0) {
              memset(&rxCi_data[0], 0, (unsigned int)k * sizeof(boolean_T));
              memset(&rxCq_data[0], 0, (unsigned int)k * sizeof(boolean_T));
            }
            /*  Remplissage des bits I et Q */
            i = (int)((((double)maxBits - 1.0) + 1.0) / 2.0);
            for (b_i = 0; b_i < i; b_i++) {
              maxBits = b_i << 1;
              rxCi_data[b_i] = (realPart_data[maxBits] != 0.0);
              rxCq_data[b_i] = (realPart_data[maxBits + 1] != 0.0);
            }
          } else {
            k = 0;
          }
          /*  Désétalement avec vérification de taille */
          rxCi[0] = k;
          rxCi[1] = k;
          rxCi[2] = 38400.0;
          rxCi[3] = 38400.0;
          minLen = minimum(rxCi);
          if (minLen > 0.0) {
            /*  Pré-allocation des résultats du désétalement */
            /*  Désétalement manuel */
            i = (int)minLen;
            maxBits = (int)minLen;
            k = (int)minLen;
            for (b_i = 0; b_i < i; b_i++) {
              Ibdn_data[b_i] = rxCi_data[b_i] ^ PRN_I[b_i];
              Qbdn_data[b_i] = rxCq_data[b_i] ^ PRN_Q[b_i];
            }
          } else {
            maxBits = 0;
            k = 0;
          }
          /*  Décodage ML */
          if ((maxBits >= 256) && (k >= 256)) {
            boolean_T Ibits_data[150];
            boolean_T Qbits_data[150];
            maxBits = (int)floor((double)maxBits / 256.0);
            numSymbols = (int)fmin(maxBits, maxBits);
            /*  Pré-allocation pour le reshape */
            k = numSymbols << 8;
            memset(&I_reshaped_data[0], 0,
                   (unsigned int)k * sizeof(signed char));
            memset(&Q_reshaped_data[0], 0,
                   (unsigned int)k * sizeof(signed char));
            /*  Remplissage manuel des matrices */
            /*  Décision ML */
            for (b_i = 0; b_i < numSymbols; b_i++) {
              maxBits = b_i << 8;
              if (maxBits + 1 > ((b_i + 1) << 8)) {
                maxBits = 0;
                i = 0;
              } else {
                i = maxBits;
              }
              for (i1 = 0; i1 < 256; i1++) {
                k = i1 + 256 * b_i;
                I_reshaped_data[k] = (signed char)Ibdn_data[maxBits + i1];
                Q_reshaped_data[k] = (signed char)Qbdn_data[i + i1];
              }
              minLen = I_reshaped_data[256 * b_i];
              startSampIdx_data = Q_reshaped_data[256 * b_i];
              for (k = 0; k < 255; k++) {
                maxBits = (k + 256 * b_i) + 1;
                minLen += (double)I_reshaped_data[maxBits];
                startSampIdx_data += (double)Q_reshaped_data[maxBits];
              }
              Ibits_data[b_i] = (minLen > 128.0);
              Qbits_data[b_i] = (startSampIdx_data > 128.0);
            }
            /*  Construction du message désétalé */
            k = numSymbols << 1;
            memset(&despreadMessage_data[0], 0,
                   (unsigned int)k * sizeof(boolean_T));
            for (b_i = 0; b_i < numSymbols; b_i++) {
              maxBits = (b_i + 1) << 1;
              despreadMessage_data[maxBits - 2] = Ibits_data[b_i];
              despreadMessage_data[maxBits - 1] = Qbits_data[b_i];
            }
            /*  Correction d'erreurs BCH */
            if (k >= 51) {
              /* Affichage du préambule (50 premiers bits) */
              printf("\n=== PRÉAMBULE (50 bits despread) ===\n");
              for (int preamble_i = 0; preamble_i < 50; preamble_i++) {
                printf("%d", despreadMessage_data[preamble_i] ? 1 : 0);
                if ((preamble_i + 1) % 10 == 0) printf(" ");
              }
              printf("\n====================================\n");

              memset(&rxPayload[0], 0, 202U * sizeof(boolean_T));
              if (k - 50 >= 202) {
                memcpy(&rxPayload[0], &despreadMessage_data[50],
                       202U * sizeof(boolean_T));
              } else {
                k -= 50;
                memcpy(&rxPayload[0], &despreadMessage_data[50],
                       (unsigned int)k * sizeof(boolean_T));
              }
              *rxStatus = true;
            }
          }
        }
      }
    }
  }
  emxFree_boolean_T(&x);
  emxFree_real_T(&realPart);
  emxFree_creal_T(&sampleBufferQPSK);
  emxFree_creal_T(&rxAGCSamples);
  emxFree_creal_T(&sampleBuffer);
}

/*
 * File trailer for dsss_receiver.c
 *
 * [EOF]
 */
