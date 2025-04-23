/* specfunc/synchrotron.c
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

#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_exp.h>
#include <gsl/gsl_sf_pow_int.h>
#include <gsl/gsl_sf_synchrotron.h>

#include "error.h"
#include "chebyshev.h"

static double synchrotron1_data[13] = {
    30.364682982501076273,
    17.079395277408394574,
    4.560132133545072889,
    0.549281246730419979,
    0.372976075069301172e-01,
    0.161362430201041242e-02,
    0.481916772120371e-04,
    0.10512425288938e-05,
    0.174638504670e-07,
    0.22815486544e-09,
    0.240443082e-11,
    0.2086588e-13,
    0.15167e-15};
static cheb_series synchrotron1_cs = {
    synchrotron1_data,
    12,
    -1.0, 1.0,
    9};

static double synchrotron2_data[12] = {
    0.4490721623532660844,
    0.898353677994187218e-01,
    0.81044573772151290e-02,
    0.4261716991089162e-03,
    0.147609631270746e-04,
    0.3628633615300e-06,
    0.66634807498e-08,
    0.949077166e-10,
    0.1079125e-11,
    0.10022e-13,
    0.77e-16,
    0.5e-18};
static cheb_series synchrotron2_cs = {
    synchrotron2_data,
    11,
    -1.0, 1.0,
    7};

static double synchrotron1a_data[23] = {
    2.1329305161355000985,
    0.741352864954200240e-01,
    0.86968099909964198e-02,
    0.11703826248775692e-02,
    0.1645105798619192e-03,
    0.240201021420640e-04,
    0.35827756389389e-05,
    0.5447747626984e-06,
    0.838802856196e-07,
    0.13069882684e-07,
    0.2053099071e-08,
    0.325187537e-09,
    0.517914041e-10,
    0.83002988e-11,
    0.13352728e-11,
    0.2159150e-12,
    0.349967e-13,
    0.56994e-14,
    0.9291e-15,
    0.152e-15,
    0.249e-16,
    0.41e-17,
    0.7e-18};
static cheb_series synchrotron1a_cs = {
    synchrotron1a_data,
    22,
    -1.0, 1.0,
    11};

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

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

int gsl_sf_synchrotron_1_e(const double x, gsl_sf_result *result)
{
  /* CHECK_POINTER(result) */

  if (x < 0.0)
  {
    DOMAIN_ERROR(result);
  }
  else if (x < 2.0 * M_SQRT2 * GSL_SQRT_DBL_EPSILON)
  {
    /* BJG: added first order correction term.  The taylor series
       is  S1(x) = ((4pi)/(sqrt(3)gamma(1/3))) * (x/2)^(1/3)
       * (1 - (gamma(1/3)/2)*(x/2)^2/3 + (3/4) * (x/2)^2 ....) */
    double z = pow(x, 1.0 / 3.0);
    double cf = 1 - 8.43812762813205e-01 * z * z;
    result->val = 2.14952824153447863671 * z * cf;
    result->err = GSL_DBL_EPSILON * result->val;
    return GSL_SUCCESS;
  }
  else if (x <= 4.0)
  {
    const double c0 = M_PI / M_SQRT3;
    const double px = pow(x, 1.0 / 3.0);
    const double px11 = gsl_sf_pow_int(px, 11);
    const double t = x * x / 8.0 - 1.0;
    gsl_sf_result result_c1;
    gsl_sf_result result_c2;
    cheb_eval_e(&synchrotron1_cs, t, &result_c1);
    cheb_eval_e(&synchrotron2_cs, t, &result_c2);
    result->val = px * result_c1.val - px11 * result_c2.val - c0 * x;
    result->err = px * result_c1.err + px11 * result_c2.err + c0 * x * GSL_DBL_EPSILON;
    result->err += 2.0 * GSL_DBL_EPSILON * fabs(result->val);
    return GSL_SUCCESS;
  }
  else if (x < -8.0 * GSL_LOG_DBL_MIN / 7.0)
  {
    const double c0 = 0.2257913526447274323630976; /* log(sqrt(pi/2)) */
    const double t = (12.0 - x) / (x + 4.0);
    gsl_sf_result result_c1;
    cheb_eval_e(&synchrotron1a_cs, t, &result_c1);
    result->val = sqrt(x) * result_c1.val * exp(c0 - x);
    result->err = 2.0 * GSL_DBL_EPSILON * result->val * (fabs(c0 - x) + 1.0);
    return GSL_SUCCESS;
  }
  else
  {
    UNDERFLOW_ERROR(result);
  }
}

int main()
{
  double x = 2.0;
  gsl_sf_result result;
  
  int status = gsl_sf_synchrotron_1_e(x, &result);
  if (status == GSL_SUCCESS)
  {
    printf("%.13e\n", result.val);
  }
  else if (status == GSL_EDOM)
  {
    fprintf(stderr, "x = %.2f: Domain Error (x < 0)\n", x);
  }
  else if (status == GSL_EUNDRFLW)
  {
    fprintf(stderr, "x = %.2f: Underflow Error (x too large)\n", x);
  }
  else
  {
    fprintf(stderr, "x = %.2f: Unknown Error, Status Code = %d\n", x, status);
  }

  return 0;
}