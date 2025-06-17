#include <stdio.h>
#include <gsl/gsl_sf_bessel.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_mode.h>
#include <gsl/gsl_sf_trig.h>
#include "bessel.h"
#include "bessel_amp_phase.h"
#include "eval.h"

/*-*-*-*-*-*-*-*-*-*-*-* Private Section *-*-*-*-*-*-*-*-*-*-*-*/

/* based on SLATEC besj0, 1977 version, w. fullerton */

/* chebyshev expansions for Bessel functions

 series for bj0        on the interval  0.          to  1.60000d+01
                                        with weighted error   7.47e-18
                                         log weighted error  17.13
                               significant figures required  16.98
                                    decimal places required  17.68

*/

static float bj0_data[13] = {
  0.100254161968939137f,
  -0.665223007764405132f,
  0.248983703498281314f,
  -0.0332527231700357697f,
  0.0023114179304694015f,
  -0.0000991127741995080f,
  0.0000028916708643998f,
  -0.0000000612108586630f,
  0.0000000009838650793f,
  -0.0000000000124235515f,
  0.0000000000001265433f,
  -0.0000000000000010619f,
  0.0000000000000000074f,
};
static cheb_series bj0_cs = {
  bj0_data,
  12,
  -1, 1,
  9};

static inline int
cheb_eval_e(const cheb_series *cs,
            const float x,
            gsl_sf_result *result)
{
  float xx = x;

  int j;
  float d = 0.0f;
  float dd = 0.0f;

  float y = (2.0f * xx - (float)cs->a - (float)cs->b) / ((float)cs->b - (float)cs->a);
  float y2 = 2.0f * y;

  float e = 0.0f;

  for (j = cs->order; j >= 1; j--)
  {
    float temp = d;
    d = y2 * d - dd + (float)cs->c[j];
    e += fabsf(y2 * temp) + fabsf(dd) + fabsf((float)cs->c[j]);
    dd = temp;
  }

  {
    float temp = d;
    d = y * d - dd + 0.5f * (float)cs->c[0];
    e += fabsf(y * temp) + fabsf(dd) + 0.5f * fabsf((float)cs->c[0]);
  }

  result->val = (double)d;
  result->err = GSL_FLT_EPSILON * (double)e + fabsf((float)cs->c[cs->order]);

  return GSL_SUCCESS;
}

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_bessel_J0_e(const double x, gsl_sf_result *result)
{
  double xx = x;

  float y = fabsf((float)xx);

  /* CHECK_POINTER(result) */

  if (y < 2.0f * GSL_SQRT_FLT_EPSILON)
  {
    result->val = 1.0;
    result->err = (double)(y * y);
    return GSL_SUCCESS;
  }
  else if (y <= 4.0f)
  {
    return cheb_eval_e(&bj0_cs, 0.125f * y * y - 1.0f, result);
  }
  else
  {
    const float z = 32.0f / (y * y) - 1.0f;
    gsl_sf_result ca;
    gsl_sf_result ct;
    gsl_sf_result cp;
    const int stat_ca = cheb_eval_e(&_gsl_sf_bessel_amp_phase_bm0_cs, z, &ca);
    const int stat_ct = cheb_eval_e(&_gsl_sf_bessel_amp_phase_bth0_cs, z, &ct);
    const int stat_cp = gsl_sf_bessel_cos_pi4_e((double)y, (double)(ct.val) / (double)y, &cp);
    const float sqrty = sqrtf(y);
    const double ampl = (0.75 + ca.val) / (double)sqrty;
    result->val = ampl * cp.val;
    result->err = fabs((double)cp.val) * ca.err / (double)sqrty + fabs(ampl) * cp.err;
    result->err += GSL_DBL_EPSILON * fabs(result->val);
    return GSL_ERROR_SELECT_3(stat_ca, stat_ct, stat_cp);
  }
}

/*-*-*-*-*-*-*-*-*-* Functions w/ Natural Prototypes *-*-*-*-*-*-*-*-*-*-*/

double gsl_sf_bessel_J0(const double x)
{
  EVAL_RESULT(gsl_sf_bessel_J0_e(x, &result));
}

int gsl_sf_bessel_cos_pi4_e(double y, double eps, gsl_sf_result *result)
{
  const float sy = sinf((float)y);
  const float cy = cosf((float)y);
  const float s = sy + cy;
  const float d = sy - cy;
  const float abs_sum = fabsf(cy) + fabsf(sy);
  float seps;
  float ceps;
  if (fabsf((float)eps) < GSL_ROOT5_FLT_EPSILON)
  {
    const float e2 = (float)eps * (float)eps;
    seps = (float)eps * (1.0f - e2 / 6.0f * (1.0f - e2 / 20.0f));
    ceps = 1.0f - e2 / 2.0f * (1.0f - e2 / 12.0f);
  }
  else
  {
    seps = sinf((float)eps);
    ceps = cosf((float)eps);
  }
  result->val = (double)(ceps * s - seps * d) / M_SQRT2;
  result->err = 2.0 * GSL_FLT_EPSILON * (fabsf(ceps) + fabsf(seps)) * (double)abs_sum / M_SQRT2;

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
  else if (y > 1.0 / GSL_SQRT_DBL_MAX)
  {
    result->err += (double)(256.0f * (float)y * GSL_SQRT_FLT_MAX);
  }

  return GSL_SUCCESS;
}

int main()
{
  double x = 1.0;
  double result;
  int count = 500000;
  while (count--)
  {
    result = gsl_sf_bessel_J0(x);
  }
  printf("%.13e\n", result);
  return 0;
}