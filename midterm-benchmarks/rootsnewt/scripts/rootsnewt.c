#include <stdio.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_roots.h>
#include <math.h>

#include "demo_fn.h"
#include "demo_fn.c"

int gsl_root_test_delta(double x1, double x0, double epsabs, double epsrel)
{
  const double tolerance = epsabs + epsrel * fabs(x1);

  if (epsrel < 0.0)
    GSL_ERROR("relative tolerance is negative", GSL_EBADTOL);

  if (epsabs < 0.0)
    GSL_ERROR("absolute tolerance is negative", GSL_EBADTOL);

  if (fabs(x1 - x0) < tolerance || x1 == x0)
    return GSL_SUCCESS;

  return GSL_CONTINUE;
}

int main(void)
{
  double result = 0;
  int status;
  int iter = 0, max_iter = 10000;
  const gsl_root_fdfsolver_type *T;
  gsl_root_fdfsolver *s;
  double x0, x = 5.0, r_expected = sqrt(5.0);
  gsl_function_fdf FDF;
  struct quadratic_params params = {1.0, 0.0, -5.0};

  FDF.f = &quadratic;
  FDF.df = &quadratic_deriv;
  FDF.fdf = &quadratic_fdf;
  FDF.params = &params;

  T = gsl_root_fdfsolver_newton;
  s = gsl_root_fdfsolver_alloc(T);
  gsl_root_fdfsolver_set(s, &FDF, x);

  // printf ("using %s method\n",
  //         gsl_root_fdfsolver_name (s));

  // printf ("%-5s %10s %10s %10s\n",
  //         "iter", "root", "err", "err(est)");
  do
  {
    iter++;
    status = gsl_root_fdfsolver_iterate(s);
    x0 = x;
    x = gsl_root_fdfsolver_root(s);
    result = x;
    status = gsl_root_test_delta(x, x0, 0, 1e-15);

    // if (status == GSL_SUCCESS)
    //   printf ("Converged:\n");

    // printf ("%5d %10.7f %+10.7f %10.7f\n",
    //         iter, x, x - r_expected, x - x0);
  } while (status == GSL_CONTINUE && iter < max_iter);

  printf("%.13e\n", result);
  gsl_root_fdfsolver_free(s);
  return status;
}
