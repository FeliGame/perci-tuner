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

static double transport2_data[18] = {
    1.671760446434538503,
    -0.147735359946794490,
    0.148213819946936338e-01,
    -0.14195330326305613e-02,
    0.1306541324415708e-03,
    -0.117155795867579e-04,
    0.10333498445756e-05,
    -0.901911304223e-07,
    0.78177169833e-08,
    -0.6744565684e-09,
    0.579946394e-10,
    -0.49747619e-11,
    0.425961e-12,
    -0.36422e-13,
    0.3111e-14,
    -0.265e-15,
    0.23e-16,
    -0.19e-17};
static cheb_series transport2_cs = {
    transport2_data,
    17,
    -1, 1,
    9};

static inline int
cheb_eval_e(const cheb_series *cs,
            const double x,
            gsl_sf_result *result)
{
  int j;
  double d = 0.0;
  double dd = 0.0;

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

  {
    double temp = d;
    d = y * d - dd + 0.5 * cs->c[0];
    e += fabs(y * temp) + fabs(dd) + 0.5 * fabs(cs->c[0]);
  }

  result->val = d;
  result->err = GSL_DBL_EPSILON * e + fabs(cs->c[cs->order]);

  return GSL_SUCCESS;
}

static double
transport_sumexp(const int numexp, const int order, const double t, double x)
{
  double rk = (double)numexp;
  double sumexp = 0.0;
  int k;
  for (k = 1; k <= numexp; k++)
  {
    double sum2 = 1.0;
    double xk = 1.0 / (rk * x);
    double xk1 = 1.0;
    int j;
    for (j = 1; j <= order; j++)
    {
      sum2 = sum2 * xk1 * xk + 1.0;
      xk1 += 1.0;
    }
    sumexp *= t;
    sumexp += sum2;
    rk -= 1.0;
  }
  return sumexp;
}

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_transport_2_e(const double x, gsl_sf_result *result)
{
  double val_infinity = 3.289868133696452873;

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
    double sumexp = transport_sumexp(numexp, 2, expf(-x), x);
    double t = 2.0 * logf(x) - x + log(sumexp);
    if (t < GSL_LOG_DBL_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      double et = expf(t);
      result->val = val_infinity - et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + fabs(t) * et);
    }
    return GSL_SUCCESS;
  }
  else if (x < 2.0 / GSL_DBL_EPSILON)
  {
    const int numexp = 1;
    double sumexp = transport_sumexp(numexp, 2, 1.0, x);
    double t = 2.0 * logf(x) - x + log(sumexp);
    if (t < GSL_LOG_DBL_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      double et = expf(t);
      result->val = val_infinity - et;
      result->err = 2.0 * GSL_DBL_EPSILON * (val_infinity + (fabs(t) + 1.0) * et);
    }
    return GSL_SUCCESS;
  }
  else
  {
    double t = 2.0 * logf(x) - x;
    if (t < GSL_LOG_DBL_EPSILON)
    {
      result->val = val_infinity;
      result->err = 2.0 * GSL_DBL_EPSILON * val_infinity;
    }
    else
    {
      double et = expf(t);
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