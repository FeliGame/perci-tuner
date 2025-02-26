#include <stdio.h>
#include <gsl/gsl_sf_bessel.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_mode.h>
#include "bessel.h"
#include "bessel_amp_phase.h"
#include <gsl/gsl_sf_trig.h>

#include <time.h>
#include <stdbool.h>

static inline int
cheb_eval_e(const cheb_series *cs,
            const double x,
            gsl_sf_result *result)
{
  double xx = x;

  int j;
  double d = 0.0;
  double dd = 0.0;

  double y = (2.0 * xx - cs->a - cs->b) / (cs->b - cs->a);
  double y2 = 2.0 * y;

  double e = 0.0;

  for (j = cs->order; j >= 1; j--)
  {
    double temp = d;
    d = y2 * d - dd + cs->c[j];
    e += fabs(y2 * temp) + fabs(dd) + fabs(cs->c[j]);
    dd = temp;
  }

  {
    double temp = d;
    d = y * d - dd + 0.5 * cs->c[0];
    e += fabs(y * temp) + fabs(dd) + 0.5 * fabs(cs->c[0]);
  }

  result->val = d;
  result->err = GSL_DBL_EPSILON * e + fabs(cs->c[cs->order]);

  return GSL_SUCCESS;
}

/*-*-*-*-*-*-*-*-*-*-*-* Private Section *-*-*-*-*-*-*-*-*-*-*-*/

/* based on SLATEC besj0, 1977 version, w. fullerton */

/* chebyshev expansions for Bessel functions

 series for bj0        on the interval  0.          to  1.60000d+01
                                        with weighted error   7.47e-18
                                         log weighted error  17.13
                               significant figures required  16.98
                                    decimal places required  17.68

*/

static double bj0_data[13] = {
    0.100254161968939137,
    -0.665223007764405132,
    0.248983703498281314,
    -0.0332527231700357697,
    0.0023114179304694015,
    -0.0000991127741995080,
    0.0000028916708643998,
    -0.0000000612108586630,
    0.0000000009838650793,
    -0.0000000000124235515,
    0.0000000000001265433,
    -0.0000000000000010619,
    0.0000000000000000074,
};
static cheb_series bj0_cs = {
    bj0_data,
    12,
    -1, 1,
    9};

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_bessel_J0_e(const double x, gsl_sf_result *result)
{
  double xx = x;

  double y = fabs(xx);

  /* CHECK_POINTER(result) */

  if (y < 2.0 * GSL_SQRT_DBL_EPSILON)
  {
    result->val = 1.0;
    result->err = y * y;
    return GSL_SUCCESS;
  }
  else if (y <= 4.0)
  {
    return cheb_eval_e(&bj0_cs, 0.125 * y * y - 1.0, result);
  }
  else
  {
    const double z = 32.0 / (y * y) - 1.0;
    gsl_sf_result ca;
    gsl_sf_result ct;
    gsl_sf_result cp;
    const int stat_ca = cheb_eval_e(&_gsl_sf_bessel_amp_phase_bm0_cs, z, &ca);
    const int stat_ct = cheb_eval_e(&_gsl_sf_bessel_amp_phase_bth0_cs, z, &ct);
    const int stat_cp = gsl_sf_bessel_cos_pi4_e(y, ct.val / y, &cp);
    const double sqrty = sqrtf(y);
    const double ampl = (0.75 + ca.val) / sqrty;
    result->val = ampl * cp.val;
    result->err = fabs(cp.val) * ca.err / sqrty + fabs(ampl) * cp.err;
    result->err += GSL_DBL_EPSILON * fabs(result->val);
    return GSL_ERROR_SELECT_3(stat_ca, stat_ct, stat_cp);
  }
}

/*-*-*-*-*-*-*-*-*-* Functions w/ Natural Prototypes *-*-*-*-*-*-*-*-*-*-*/

#include "eval.h"

double gsl_sf_bessel_J0(const double x)
{
  // printf("flag\n");
  EVAL_RESULT(gsl_sf_bessel_J0_e(x, &result));
}

int gsl_sf_bessel_cos_pi4_e(double y, double eps, gsl_sf_result *result)
{
  const double sy = sinf(y);
  const double cy = cosf(y);
  const double s = sy + cy;
  const double d = sy - cy;
  const double abs_sum = fabs(cy) + fabs(sy);
  double seps;
  double ceps;
  if (fabs(eps) < GSL_ROOT5_DBL_EPSILON)
  {
    const double e2 = eps * eps;
    seps = eps * (1.0 - e2 / 6.0 * (1.0 - e2 / 20.0));
    ceps = 1.0 - e2 / 2.0 * (1.0 - e2 / 12.0);
  }
  else
  {
    seps = sinf(eps);
    ceps = cosf(eps);
  }
  result->val = (ceps * s - seps * d) / M_SQRT2;
  result->err = 2.0 * GSL_DBL_EPSILON * (fabs(ceps) + fabs(seps)) * abs_sum / M_SQRT2;

  /* Try to account for error in evaluation of sin(y), cos(y).
   * This is a little sticky because we don't really know
   * how the library routines are doing their argument reduction.
   * However, we will make a reasonable guess.
   * FIXME ?
   */
  if (y > 1.0 / GSL_DBL_EPSILON)
  {
    result->err *= 0.5 * y;
  }
  else if (y > 1.0 / GSL_SQRT_DBL_EPSILON)
  {
    result->err *= 256.0 * y * GSL_SQRT_DBL_EPSILON;
  }

  return GSL_SUCCESS;
}

int main(void)
{
  double x = 5.0;
  int count = 5000000;
  double y = 0;
  while (count--)
  {
    /* code */
    y = gsl_sf_bessel_J0(x);
  }
  printf("%.16f\n", y);

  return 0;
}
