#include <stdio.h>
#include <gsl/gsl_sf_bessel.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_mode.h>
#include "bessel.h"
#include "bessel_amp_phase.h"
#include <gsl/gsl_sf_trig.h>
#include "error.h"
#include "eval.h"

#define ROOT_EIGHT (2.0 * M_SQRT2)

/*-*-*-*-*-*-*-*-*-*-*-* Private Section *-*-*-*-*-*-*-*-*-*-*-*/

/* based on SLATEC besj0, 1977 version, w. fullerton */

/* chebyshev expansions for Bessel functions

 series for bj0        on the interval  0.          to  1.60000d+01
                                        with weighted error   7.47e-18
                                         log weighted error  17.13
                               significant figures required  16.98
                                    decimal places required  17.68

*/

static double bj1_data[12] = {
    -0.11726141513332787,
    -0.25361521830790640,
    0.050127080984469569,
    -0.004631514809625081,
    0.000247996229415914,
    -0.000008678948686278,
    0.000000214293917143,
    -0.000000003936093079,
    0.000000000055911823,
    -0.000000000000632761,
    0.000000000000005840,
    -0.000000000000000044,
};
static cheb_series bj1_cs = {
    bj1_data,
    11,
    -1, 1,
    8};

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

int gsl_sf_bessel_sin_pi4_e(double y, double eps, gsl_sf_result *result)
{
  const double sy = sin(y);
  const double cy = cos(y);
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
    seps = sin(eps);
    ceps = cos(eps);
  }
  result->val = (ceps * d + seps * s) / M_SQRT2;
  result->err = 2.0 * GSL_DBL_EPSILON * (fabs(ceps) + fabs(seps)) * abs_sum / M_SQRT2;

  /* Try to account for error in evaluation of sin(y), cos(y).
   * See above.
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

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_bessel_J1_e(const double x, gsl_sf_result *result)
{
  double y = fabs(x);

  /* CHECK_POINTER(result) */

  if (y == 0.0)
  {
    result->val = 0.0;
    result->err = 0.0;
    return GSL_SUCCESS;
  }
  else if (y < 2.0 * GSL_DBL_MIN)
  {
    UNDERFLOW_ERROR(result);
  }
  else if (y < ROOT_EIGHT * GSL_SQRT_DBL_EPSILON)
  {
    result->val = 0.5 * x;
    result->err = 0.0;
    return GSL_SUCCESS;
  }
  else if (y < 4.0)
  {
    gsl_sf_result c;
    cheb_eval_e(&bj1_cs, 0.125 * y * y - 1.0, &c);
    result->val = x * (0.25 + c.val);
    result->err = fabs(x * c.err);
    return GSL_SUCCESS;
  }
  else
  {
    /* Because the leading term in the phase is y,
     * which we assume is exactly known, the error
     * in the cos() evaluation is bounded.
     */
    const double z = 32.0 / (y * y) - 1.0;
    gsl_sf_result ca;
    gsl_sf_result ct;
    gsl_sf_result sp;
    const int stat_ca = cheb_eval_e(&_gsl_sf_bessel_amp_phase_bm1_cs, z, &ca);
    const int stat_ct = cheb_eval_e(&_gsl_sf_bessel_amp_phase_bth1_cs, z, &ct);
    const int stat_sp = gsl_sf_bessel_sin_pi4_e(y, ct.val / y, &sp);
    const double sqrty = sqrt(y);
    const double ampl = (0.75 + ca.val) / sqrty;
    result->val = (x < 0.0 ? -ampl : ampl) * sp.val;
    result->err = fabs(sp.val) * ca.err / sqrty + fabs(ampl) * sp.err;
    result->err += GSL_DBL_EPSILON * fabs(result->val);
    return GSL_ERROR_SELECT_3(stat_ca, stat_ct, stat_sp);
  }
}

/*-*-*-*-*-*-*-*-*-* Functions w/ Natural Prototypes *-*-*-*-*-*-*-*-*-*-*/

double gsl_sf_bessel_J1(const double x)
{
  EVAL_RESULT(gsl_sf_bessel_J1_e(x, &result));
}

int main(void)
{
  double x = 5.0;
  double result = 0.0;
  int count = 500000;
  while (count--)
  {
    result = gsl_sf_bessel_J1(x);
  }
  printf("%.13e\n", result);
  return 0;
}
