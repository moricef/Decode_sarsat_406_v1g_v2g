/*
 * sign.c
 *
 * Code generation for function 'sign'
 *
 */

/* Include files */
#include "sign.h"
#include "rt_nonfinite.h"
#include "test_dsss_receiver_types.h"
#include "mwmathutil.h"

/* Function Definitions */
void b_sign(emxArray_creal_T *x)
{
  creal_T *x_data;
  int32_T k;
  int32_T nx;
  x_data = x->data;
  nx = x->size[0];
  for (k = 0; k < nx; k++) {
    real_T xi;
    real_T xr;
    xr = x_data[k].re;
    xi = x_data[k].im;
    if (xi == 0.0) {
      x_data[k].re = muDoubleScalarSign(xr);
      x_data[k].im = 0.0;
    } else {
      real_T absx;
      if ((muDoubleScalarAbs(xr) > 8.9884656743115785E+307) ||
          (muDoubleScalarAbs(xi) > 8.9884656743115785E+307)) {
        xr /= 2.0;
        xi /= 2.0;
      }
      absx = muDoubleScalarHypot(xr, xi);
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

/* End of code generation (sign.c) */
