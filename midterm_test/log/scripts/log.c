#include <stdio.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_result.h>
#include "error.h"
#include "chebyshev.h"

struct cheb_series
{
  double *c;
  int order;
  double a, b;
  int order_sp;
};

static inline int cheb_eval_e(const struct cheb_series *cs, double x, gsl_sf_result *result)
{
  int j;
  double d = 0.0, dd = 0.0;
  double y = (2.0 * x - cs->a - cs->b) / (cs->b - cs->a);
  double y2 = 2.0 * y;
  double e = 0.0;

  for (j = cs->order; j >= 1; j--)
  {
    double temp = d;
    d = y2 * d - dd + cs->c[j];
    e += fabs(y2 * temp) + fabs(dd) + fabs(cs->c[j]);
    dd = temp;
  }

  double temp = d;
  d = y * d - dd + 0.5 * cs->c[0];
  e += fabs(y * temp) + fabs(dd) + 0.5 * fabs(cs->c[0]);

  result->val = d;
  result->err = GSL_DBL_EPSILON * e + fabs(cs->c[cs->order]);

  return GSL_SUCCESS;
}

/* Chebyshev expansion for log(1 + x)/x */
static double lopx_data[21] = {
    2.1664791066439527, -0.2856539855104974, 0.0151776725569055, -0.0020021590494142,
    0.000192113751640567, -0.0000255325888610554, 2.90045126604006e-06,
    -3.88738135170573e-07, 4.77436787294005e-08, -6.45019697760903e-09,
    8.27519766288124e-10, -1.1260499376492e-10, 1.48445766922709e-11,
    -2.03285159724621e-12, 2.72912312205492e-13, -3.75819778303879e-14,
    5.11073458708617e-15, -7.07221500114333e-16, 9.70897583282485e-17,
    -1.34926374575219e-17, 1.86573279106773e-18};

static struct cheb_series lopx_cs = {
    lopx_data,
    20,
    -1, 1,
    10};

int gsl_sf_log_1plusx_e(const double x, gsl_sf_result *result)
{
  if (x <= -1.0)
  {
    result->val = 0.0;
    result->err = 0.0;
    return GSL_EDOM;
  }

  if (fabs(x) < GSL_ROOT6_DBL_EPSILON)
  {
    const double c1 = -0.5, c2 = 1.0 / 3.0, c3 = -1.0 / 4.0;
    const double c4 = 1.0 / 5.0, c5 = -1.0 / 6.0, c6 = 1.0 / 7.0;
    const double c7 = -1.0 / 8.0, c8 = 1.0 / 9.0, c9 = -1.0 / 10.0;
    const double t = c5 + x * (c6 + x * (c7 + x * (c8 + x * c9)));
    result->val = x * (1.0 + x * (c1 + x * (c2 + x * (c3 + x * (c4 + x * t)))));
    result->err = GSL_DBL_EPSILON * fabs(result->val);
    return GSL_SUCCESS;
  }
  else if (fabs(x) < 0.5)
  {
    double t = 0.5 * (8.0 * x + 1.0) / (x + 2.0);
    gsl_sf_result c;
    cheb_eval_e(&lopx_cs, t, &c);
    result->val = x * c.val;
    result->err = fabs(x * c.err);
    return GSL_SUCCESS;
  }
  else
  {
    result->val = log(1.0 + x);
    result->err = GSL_DBL_EPSILON * fabs(result->val);
    return GSL_SUCCESS;
  }
}

int main()
{
  double x = -0.9;
  gsl_sf_result result;
  int status = gsl_sf_log_1plusx_e(x, &result);

  if (status == GSL_SUCCESS)
    printf("%.13e\n", result.val);
  else
    printf("Error for log(1 + %f)\n", x);

  return 0;
}