/* specfunc/transport.c
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 Gerard Jungman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/* Author:  G. Jungman */
#include <stdio.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_transport.h>

#include "error.h"

struct cheb_series_struct {
  double * c;   /* coefficients                */
  int order;    /* order of expansion          */
  double a;     /* lower interval point        */
  double b;     /* upper interval point        */
  int order_sp; /* effective single precision order */
};
typedef struct cheb_series_struct cheb_series;

static float transport2_data[18] = {
    1.6717604f, -0.14773536f, 0.014821382f, -0.001419533f,
    0.0001306541f, -0.0000117156f, 1.0333e-06f, -9.019e-08f,
    7.8177e-09f, -6.7446e-10f, 5.799e-11f, -4.9748e-12f,
    4.2596e-13f, -3.6422e-14f, 3.111e-15f, -2.65e-16f,
    2.3e-17f, -1.9e-18f
};

static cheb_series transport2_cs = {
    (double *)transport2_data,
    17,
    -1, 1,
    9};

static inline int
cheb_eval_e(const cheb_series *cs, const double x, gsl_sf_result *result)
{
  float d = 0.0f, dd = 0.0f;
  float y = (2.0f * (float)x - (float)cs->a - (float)cs->b) / ((float)cs->b - (float)cs->a);
  float y2 = 2.0f * y;
  double e = 0.0;

  for (int j = cs->order; j >= 1; j--) {
    float temp = d;
    d = y2 * d - dd + (float)cs->c[j];
    e += fabs(y2 * temp) + fabs(dd) + fabs(cs->c[j]);
    dd = temp;
  }

  float temp = d;
  d = y * d - dd + 0.5f * (float)cs->c[0];
  e += fabs(y * temp) + fabs(dd) + 0.5 * fabs(cs->c[0]);

  result->val = d;
  result->err = GSL_DBL_EPSILON * e + fabs(cs->c[cs->order]);
  return GSL_SUCCESS;
}


static double
transport_sumexp(const int numexp, const int order, const double t, double x)
{
  float rk = (float)numexp;
  float sumexp = 0.0f;

  for (int k = 1; k <= numexp; k++) {
    float sum2 = 1.0f;
    float xk = 1.0f / (rk * (float)x);
    float xk1 = 1.0f;

    for (int j = 1; j <= order; j++) {
      sum2 = sum2 * xk1 * xk + 1.0f;
      xk1 += 1.0f;
    }

    sumexp *= (float)t;
    sumexp += sum2;
    rk -= 1.0f;
  }
  return (double)sumexp;
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
  else if (x < 3.0 * GSL_SQRT_DBL_EPSILON)
  {
    result->val = x;
    result->err = GSL_DBL_EPSILON * fabs(x) + x * x / 2.0;
    return GSL_SUCCESS;
  }
  else if (x <= 4.0)
  {
    double t = (x * x / 8.0 - 0.5) - 0.5;
    gsl_sf_result result_c;
    cheb_eval_e(&transport2_cs, t, &result_c);
    result->val = x * result_c.val;
    result->err = x * result_c.err;
    result->err += 2.0 * GSL_DBL_EPSILON * fabs(result->val);
    return GSL_SUCCESS;
  }
  else if (x < -GSL_LOG_DBL_EPSILON)
  {
    const int numexp = (int)((-GSL_LOG_DBL_EPSILON) / x) + 1;
    const double sumexp = transport_sumexp(numexp, 2, exp(-x), x);
    const double t = 2.0 * log(x) - x + log(sumexp);
    if (t < GSL_LOG_DBL_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      const double et = exp(t);
      result->val = val_infinity - et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + fabs(t) * et);
    }
    return GSL_SUCCESS;
  }
  else if (x < 2.0 / GSL_DBL_EPSILON)
  {
    const int numexp = 1;
    const double sumexp = transport_sumexp(numexp, 2, 1.0, x);
    const double t = 2.0 * log(x) - x + log(sumexp);
    if (t < GSL_LOG_DBL_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      const double et = exp(t);
      result->val = val_infinity - et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + (fabs(t) + 1.0) * et);
    }
    return GSL_SUCCESS;
  }
  else
  {
    const double t = 2.0 * log(x) - x;
    if (t < GSL_LOG_DBL_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      const double et = exp(t);
      result->val = val_infinity - et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + (fabs(t) + 1.0) * et);
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