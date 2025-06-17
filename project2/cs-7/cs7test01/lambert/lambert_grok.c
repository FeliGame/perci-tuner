#include <stdio.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_lambert.h>

/* Started with code donated by K. Briggs; added
 * error estimates, GSL foo, and minor tweaks.
 * Some Lambert-ology from
 *  [Corless, Gonnet, Hare, and Jeffrey, "On Lambert's W Function".]
 */

/* Halley iteration (eqn. 5.12, Corless et al) */
static int
halley_iteration(
    double x,
    double w_initial,
    unsigned int max_iters,
    gsl_sf_result *result)
{
  double w = w_initial;
  unsigned int i;

  for (i = 0; i < max_iters; i++)
  {
    float tol;
    const double e = exp(w);
    const double p = w + 1.0;
    double t = w * e - x;
    /* printf("FOO: %20.16g  %20.16g\n", w, t); */

    if (w > 0)
    {
      t = (t / p) / e; /* Newton iteration */
    }
    else
    {
      t /= e * p - 0.5 * (p + 1.0) * t / p; /* Halley iteration */
    };

    w -= t;

    tol = 10.0f * GSL_FLT_EPSILON * GSL_MAX_DBL(fabs(w), 1.0 / (fabs(p) * e));

    if (fabs(t) < tol)
    {
      result->val = w;
      result->err = 2.0 * tol;
      return GSL_SUCCESS;
    }
  }

  /* should never get here */
  result->val = w;
  result->err = fabs(w);
  return GSL_EMAXITER;
}

/* series which appears for q near zero;
 * only the argument is different for the different branches
 */
static double
series_eval(float r)
{
  static const float c[12] = {
      -1.0f,
      2.331643981597124203363536062168f,
      -1.812187885639363490240191647568f,
      1.936631114492359755363277457668f,
      -2.353551201881614516821543561516f,
      3.066858901050631912893148922704f,
      -4.175335600258177138854984177460f,
      5.858023729874774148815053846119f,
      -8.401032217523977370984161688514f,
      12.250753501314460424f,
      -18.100697012472442755f,
      27.029044799010561650f};
  const float t_8 = c[8] + r * (c[9] + r * (c[10] + r * c[11]));
  const float t_5 = c[5] + r * (c[6] + r * (c[7] + r * t_8));
  const float t_1 = c[1] + r * (c[2] + r * (c[3] + r * (c[4] + r * t_5)));
  return c[0] + r * t_1;
}

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_lambert_W0_e(double x, gsl_sf_result *result)
{
  const float one_over_E = 1.0f / M_E;
  const float q = x + one_over_E;

  if (x == 0.0)
  {
    result->val = 0.0;
    result->err = 0.0;
    return GSL_SUCCESS;
  }
  else if (q < 0.0f)
  {
    /* Strictly speaking this is an error. But because of the
     * arithmetic operation connecting x and q, I am a little
     * lenient in case of some epsilon overshoot. The following
     * answer is quite accurate in that case. Anyway, we have
     * to return GSL_EDOM.
     */
    result->val = -1.0;
    result->err = sqrtf(-q);
    return GSL_EDOM;
  }
  else if (q == 0.0f)
  {
    result->val = -1.0;
    result->err = GSL_FLT_EPSILON; /* cannot error is zero, maybe q == 0 by "accident" */
    return GSL_SUCCESS;
  }
  else if (q < 1.0e-03f)
  {
    /* series near -1/E in sqrt(q) */
    const float r = sqrtf(q);
    result->val = series_eval(r);
    result->err = 2.0f * GSL_FLT_EPSILON * fabsf(result->val);
    return GSL_SUCCESS;
  }
  else
  {
    static const unsigned int MAX_ITERS = 10;
    float w;

    if (x < 1.0)
    {
      /* obtain initial approximation from series near x=0;
       * no need for extra care, since the Halley iteration
       * converges nicely on this branch
       */
      const float p = sqrtf(2.0f * M_E * q);
      w = -1.0f + p * (1.0f + p * (-1.0f / 3.0f + p * 11.0f / 72.0f));
    }
    else
    {
      /* obtain initial approximation from rough asymptotic */
      w = logf(x);
      if (x > 3.0)
        w -= logf(w);
    }

    return halley_iteration(x, w, MAX_ITERS, result);
  }
}

/*-*-*-*-*-*-*-*-*-* Functions w/ Natural Prototypes *-*-*-*-*-*-*-*-*-*-*/

int main()
{
  double x = 1.0;
  gsl_sf_result result;

  int status = 99;
  int count = 1000000;
  while (count--)
  {
    status = gsl_sf_lambert_W0_e(x, &result);
  }
  if (status == GSL_SUCCESS)
    printf("%.13e\n", result.val);
  else
    printf("Error for W_{-1}(%f)\n", x);

  return 0;
}