/*
 * File: sign.c
 *
 * MATLAB Coder version            : 24.1
 * C/C++ source code generated on  : 09-Nov-2025 12:47:24
 */

/* Include Files */
#include "sign.h"
#include "dsss_receiver_rtwutil.h"
#include "dsss_receiver_types.h"
#include "rt_nonfinite.h"
#include "rt_nonfinite.h"
#include <math.h>

/* Function Definitions */
/*
 * Arguments    : emxArray_creal_T *x
 * Return Type  : void
 */
void b_sign(emxArray_creal_T *x)
{
  creal_T *x_data;
  int k;
  int nx;
  x_data = x->data;
  nx = x->size[0];
  for (k = 0; k < nx; k++) {
    double xi;
    double xr;
    xr = x_data[k].re;
    xi = x_data[k].im;
    if (xi == 0.0) {
      if (rtIsNaN(xr)) {
        x_data[k].re = rtNaN;
      } else if (xr < 0.0) {
        x_data[k].re = -1.0;
      } else {
        x_data[k].re = (xr > 0.0);
      }
      x_data[k].im = 0.0;
    } else {
      double absx;
      if ((fabs(xr) > 8.9884656743115785E+307) ||
          (fabs(xi) > 8.9884656743115785E+307)) {
        xr /= 2.0;
        xi /= 2.0;
      }
      absx = rt_hypotd_snf(xr, xi);
      if (absx == 0.0) {
        x_data[k].re = 0.0;
        x_data[k].im = 0.0;
      } else {
        x_data[k].re = xr / absx;
        x_data[k].im = xi / absx;
      }
    }
  }
}

/*
 * File trailer for sign.c
 *
 * [EOF]
 */
