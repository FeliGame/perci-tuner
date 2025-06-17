#include <stdio.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_roots.h>
#include <math.h>

struct quadratic_params
{
  float a; // Changed to float
  float b;
  float c;
};

double quadratic(double x, void *params)
{
  struct quadratic_params *p = (struct quadratic_params *)params;

  float a = p->a;
  float b = p->b;
  float c = p->c;

  return (double)((a * (float)x + b) * (float)x + c); // Compute in float, cast to double
}

double quadratic_deriv(double x, void *params)
{
  struct quadratic_params *p = (struct quadratic_params *)params;

  float a = p->a;
  float b = p->b;

  return (double)(2.0f * a * (float)x + b); // Compute in float, cast to double
}

void quadratic_fdf(double x, void *params,
                   double *y, double *dy)
{
  struct quadratic_params *p = (struct quadratic_params *)params;

  float a = p->a;
  float b = p->b;
  float c = p->c;

  *y = (double)((a * (float)x + b) * (float)x + c); // Compute in float
  *dy = (double)(2.0f * a * (float)x + b); // Compute in float
}

int gsl_root_test_interval(double x_lower, double x_upper, float epsabs, float epsrel)
{
  const double abs_lower = fabs(x_lower);
  const double abs_upper = fabs(x_upper);

  double min_abs;
  double tolerance;

  if (epsrel < 0.0f)
    GSL_ERROR("relative tolerance is negative", GSL_EBADTOL);

  if (epsabs < 0.0f)
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

  tolerance = (double)epsabs + (double)epsrel * min_abs;

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
  double r = 0;
  double r_expected = sqrt(5.0);
  double x_lo = 0.0;
  double x_hi = 5.0;
  gsl_function F;
  struct quadratic_params params = {1.0f, 0.0f, -5.0f}; // Changed to float

  F.function = &quadratic;
  F.params = &params;

  T = gsl_root_fsolver_brent;
  s = gsl_root_fsolver_alloc(T);
  gsl_root_fsolver_set(s, &F, x_lo, x_hi);

  do
  {
    iter++;
    status = gsl_root_fsolver_iterate(s);
    r = gsl_root_fsolver_root(s);
    x_lo = gsl_root_fsolver_x_lower(s);
    x_hi = gsl_root_fsolver_x_upper(s);
    status = gsl_root_test_interval(x_lo, x_hi,
                                    0.0f, 0.001f); // Changed to float

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