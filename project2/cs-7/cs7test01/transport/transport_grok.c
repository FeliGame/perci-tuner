#include <stdio.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_transport.h>

#include "error.h"

struct cheb_series_struct {
  float * c;   /* coefficients                */
  int order;    /* order of expansion          */
  float a;     /* lower interval point        */
  float b;     /* upper interval point        */
  int order_sp; /* effective single precision order */
};
typedef struct cheb_series_struct cheb_series;

static float transport2_data[18] = {
    1.671760446434538503f,
    -0.147735359946794490f,
    0.148213819946936338e-01f,
    -0.14195330326305613e-02f,
    0.1306541324415708e-03f,
    -0.117155795867579e-04f,
    0.10333498445756e-05f,
    -0.901911304223e-07f,
    0.78177169833e-08f,
    -0.6744565684e-09f,
    0.579946394e-10f,
    -0.49747619e-11f,
    0.425961e-12f,
    -0.36422e-13f,
    0.3111e-14f,
    -0.265e-15f,
    0.23e-16f,
    -0.19e-17f};
static cheb_series transport2_cs = {
    transport2_data,
    17,
    -1, 1,
    9};

static inline int
cheb_eval_e(const cheb_series *cs,
            const float x,
            gsl_sf_result *result)
{
  int j;
  float d = 0.0f;
  float dd = 0.0f;

  float y = (2.0f * x - cs->a - cs->b) / (cs->b - cs->a);
  float y2 = 2.0f * y;

  float e = 0.0f;

  for (j = cs->order; j >= 1; j--)
  {
    float temp = d;
    d = y2 * d - dd + cs->c[j];
    e += fabsf(y2 * temp) + fabsf(dd) + fabsf(cs->c[j]);
    dd = temp;
  }

  {
    float temp = d;
    d = y * d - dd + 0.5f * cs->c[0];
    e += fabsf(y * temp) + fabsf(dd) + 0.5f * fabsf(cs->c[0]);
  }

  result->val = (double)d;
  result->err = GSL_FLT_EPSILON * (double)e + fabsf(cs->c[cs->order]);

  return GSL_SUCCESS;
}

static float
transport_sumexp(const int numexp, const int order, const float t, float x)
{
  float rk = (float)numexp;
  float sumexp = 0.0f;
  int k;
  for (k = 1; k <= numexp; k++)
  {
    float sum2 = 1.0f;
    float xk = 1.0f / (rk * x);
    float xk1 = 1.0f;
    int j;
    for (j = 1; j <= order; j++)
    {
      sum2 = sum2 * xk1 * xk + 1.0f;
      xk1 += 1.0f;
    }
    sumexp *= t;
    sumexp += sum2;
    rk -= 1.0f;
  }
  return sumexp;
}

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_transport_2_e(const double x, gsl_sf_result *result)
{
  const double val_infinity = 3.289868133696452873;

  /* CHECK_POINTER(result) */

  if (x < 0.0)
  {
    DOMAIN_ERROR(result);
  }
  else if (x < 3.0 * GSL_SQRT_FLT_EPSILON)
  {
    result->val = x;
    result->err = GSL_FLT_EPSILON * fabsf((float)x) + (double)((float)x * (float)x / 2.0f);
    return GSL_SUCCESS;
  }
  else if (x <= 4.0)
  {
    float t = ((float)x * (float)x / 8.0f - 0.5f) - 0.5f;
    gsl_sf_result result_c;
    cheb_eval_e(&transport2_cs, t, &result_c);
    result->val = x * result_c.val;
    result->err = x * result_c.err;
    result->err += 2.0 * GSL_DBL_EPSILON * fabs(result->val);
    return GSL_SUCCESS;
  }
  else if (x < -GSL_LOG_FLT_EPSILON)
  {
    const int numexp = (int)((-GSL_LOG_FLT_EPSILON) / (float)x) + 1;
    const float sumexp = transport_sumexp(numexp, 2, expf(-(float)x), (float)x);
    const float t = 2.0f * logf((float)x) - (float)x + logf(sumexp);
    if (t < GSL_LOG_FLT_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      const float et = expf(t);
      result->val = val_infinity - (double)et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + (double)(fabsf(t) * et));
    }
    return GSL_SUCCESS;
  }
  else if (x < 2.0 / GSL_FLT_EPSILON)
  {
    const int numexp = 1;
    const float sumexp = transport_sumexp(numexp, 2, 1.0f, (float)x);
    const float t = 2.0f * logf((float)x) - (float)x + logf(sumexp);
    if (t < GSL_LOG_FLT_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      const float et = expf(t);
      result->val = val_infinity - (double)et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + (double)(fabsf(t) + 1.0f) * et);
    }
    return GSL_SUCCESS;
  }
  else
  {
    const float t = 2.0f * logf((float)x) - (float)x;
    if (t < GSL_LOG_FLT_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      const float et = expf(t);
      result->val = val_infinity - (double)et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + (double)(fabsf(t) + 1.0f) * et);
    }
    return GSL_SUCCESS;
  }
}

int main()
{
  double x = 3.0;
  gsl_sf_result result;
  int status = 99;
  int count = 1000000;
  while (count--)
  {
    status = gsl_sf_transport_2_e(x, &result);
  }
  if (status == GSL_SUCCESS)
    printf("%.13e\n", result.val);
  else
    printf("Error for transport_2(%f)\n", x);
  return 0;
}