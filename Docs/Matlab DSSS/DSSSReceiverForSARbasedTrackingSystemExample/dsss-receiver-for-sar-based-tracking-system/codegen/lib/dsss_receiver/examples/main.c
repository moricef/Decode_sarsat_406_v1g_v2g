/*
 * File: main.c
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

/*************************************************************************/
/* This automatically generated example C main file shows how to call    */
/* entry-point functions that MATLAB Coder generated. You must customize */
/* this file for your application. Do not modify this file directly.     */
/* Instead, make a copy of this file, modify it, and integrate it into   */
/* your development environment.                                         */
/*                                                                       */
/* This file initializes entry-point function arguments to a default     */
/* size and value before calling the entry-point functions. It does      */
/* not store or use any values returned from the entry-point functions.  */
/* If necessary, it does pre-allocate memory for returned values.        */
/* You can use this file as a starting point for a main function that    */
/* you can deploy in your application.                                   */
/*                                                                       */
/* After you copy the file, and before you deploy it, you must make the  */
/* following changes:                                                    */
/* * For variable-size function arguments, change the example sizes to   */
/* the sizes that your application requires.                             */
/* * Change the example values of function arguments to the values that  */
/* your application requires.                                            */
/* * If the entry-point functions return values, store these values or   */
/* otherwise use them as required by your application.                   */
/*                                                                       */
/*************************************************************************/

/* Include Files */
#include "main.h"
#include "dsss_receiver.h"
#include "dsss_receiver_terminate.h"
#include "dsss_receiver_types.h"
#include "rt_nonfinite.h"

/* Function Declarations */
static void argInit_307200x1_creal_T(creal_T result[307200]);

static creal_T argInit_creal_T(void);

static double argInit_real_T(void);

static struct0_T argInit_struct0_T(void);

/* Function Definitions */
/*
 * Arguments    : creal_T result[307200]
 * Return Type  : void
 */
static void argInit_307200x1_creal_T(creal_T result[307200])
{
  int idx0;
  /* Loop over the array to initialize each element. */
  for (idx0 = 0; idx0 < 307200; idx0++) {
    /* Set the value of the array element.
Change this value to the value that the application requires. */
    result[idx0] = argInit_creal_T();
  }
}

/*
 * Arguments    : void
 * Return Type  : creal_T
 */
static creal_T argInit_creal_T(void)
{
  creal_T result;
  double re_tmp;
  /* Set the value of the complex variable.
Change this value to the value that the application requires. */
  re_tmp = argInit_real_T();
  result.re = re_tmp;
  result.im = re_tmp;
  return result;
}

/*
 * Arguments    : void
 * Return Type  : double
 */
static double argInit_real_T(void)
{
  return 0.0;
}

/*
 * Arguments    : void
 * Return Type  : struct0_T
 */
static struct0_T argInit_struct0_T(void)
{
  struct0_T result;
  double result_tmp;
  /* Set the value of each structure field.
Change this value to the value that the application requires. */
  result_tmp = argInit_real_T();
  result.velocity = result_tmp;
  result.txPower = result_tmp;
  result.oversampling = result_tmp;
  return result;
}

/*
 * Arguments    : int argc
 *                char **argv
 * Return Type  : int
 */
int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  /* The initialize function is being called automatically from your entry-point
   * function. So, a call to initialize is not included here. */
  /* Invoke the entry-point functions.
You can call entry-point functions multiple times. */
  main_dsss_receiver();
  /* Terminate the application.
You do not need to do this more than one time. */
  dsss_receiver_terminate();
  return 0;
}

/*
 * Arguments    : void
 * Return Type  : void
 */
void main_dsss_receiver(void)
{
  static creal_T dcv[307200];
  struct0_T r;
  int errs;
  boolean_T rxPayload[202];
  boolean_T rxStatus;
  /* Initialize function 'dsss_receiver' input arguments. */
  /* Initialize function input argument 'otaBuffer'. */
  /* Initialize function input argument 'settings'. */
  /* Call the entry-point 'dsss_receiver'. */
  argInit_307200x1_creal_T(dcv);
  r = argInit_struct0_T();
  dsss_receiver(dcv, &r, rxPayload, &errs, &rxStatus);
}

/*
 * File trailer for main.c
 *
 * [EOF]
 */
