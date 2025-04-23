#include <stdio.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_roots.h>
#include <math.h>
#include "demo_fn.h"
#include "demo_fn.c"

int gsl_root_test_interval(double x_lower, double x_upper, double epsabs, double epsrel)
{
  const double abs_lower = fabs(x_lower);
  const double abs_upper = fabs(x_upper);

  double min_abs, tolerance;

  if (epsrel < 0.0)
    GSL_ERROR("relative tolerance is negative", GSL_EBADTOL);

  if (epsabs < 0.0)
    GSL_ERROR("absolute tolerance is negative", GSL_EBADTOL);

  if (x_lower > x_upper)
    GSL_ERROR("lower bound larger than upper bound", GSL_EINVAL);

  if ((x_lower > 0.0 && x_upper > 0.0) || (x_lower < 0.0 && x_upper < 0.0))
  {
    min_abs = GSL_MIN_DBL(abs_lower, abs_upper);
  }
  else
  {
    min_abs = 0;
  }

  tolerance = epsabs + epsrel * min_abs;

  if (fabs(x_upper - x_lower) < tolerance)
    return GSL_SUCCESS;

  return GSL_CONTINUE;
}

int main(void)
{
  int status;
  int iter = 0, max_iter = 100;
  const gsl_root_fsolver_type *T;
  gsl_root_fsolver *s;
  double r = 0, r_expected = sqrt(5.0);
  double x_lo = 0.0, x_hi = 5.0;
  gsl_function F;
  struct quadratic_params params = {1.0, 0.0, -5.0};

  F.function = &quadratic;
  F.params = &params;

  T = gsl_root_fsolver_brent;
  s = gsl_root_fsolver_alloc(T);
  gsl_root_fsolver_set(s, &F, x_lo, x_hi);

  // printf("using %s method\n",
  //        gsl_root_fsolver_name(s));

  // printf("%5s [%9s, %9s] %9s %10s %9s\n",
  //        "iter", "lower", "upper", "root",
  //        "err", "err(est)");

  do
  {
    iter++;
    status = gsl_root_fsolver_iterate(s);
    r = gsl_root_fsolver_root(s);
    x_lo = gsl_root_fsolver_x_lower(s);
    x_hi = gsl_root_fsolver_x_upper(s);
    status = gsl_root_test_interval(x_lo, x_hi,
                                    0, 0.001);

    // if (status == GSL_SUCCESS)
    //   printf("Converged:\n");

    // printf("%5d [%.7f, %.7f] %.7f %+.7f %.7f\n",
    //        iter, x_lo, x_hi,
    //        r, r - r_expected,
    //        x_hi - x_lo);
  } while (status == GSL_CONTINUE && iter < max_iter);

  printf("%.13e\n", r);
  gsl_root_fsolver_free(s);

  return status;
}
