/* specfunc/ellint.c
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

/* Author: G. Jungman */

#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_precision.h>
#include <gsl/gsl_sf_ellint.h>

#include <time.h>
#include <stdbool.h>

#include "error.h"

/*-*-*-*-*-*-*-*-*-*-*-* Private Section *-*-*-*-*-*-*-*-*-*-*-*/

inline static double locMAX3(double x, double y, double z)
{
  double xy = GSL_MAX(x, y);
  return GSL_MAX(xy, z);
}

inline static double locMAX4(double x, double y, double z, double w)
{
  double xy = GSL_MAX(x, y);
  double xyz = GSL_MAX(xy, z);
  return GSL_MAX(xyz, w);
}

/*-*-*-*-*-*-*-*-*-*-*-* Functions with Error Codes *-*-*-*-*-*-*-*-*-*-*-*/

/* based on Carlson's algorithms:
   [B. C. Carlson Numer. Math. 33, 1 (1979)]

   see also:
   [B.C. Carlson, Special Functions of Applied Mathematics (1977)]
 */

/* According to Carlson's algorithm, the errtol parameter
   typically effects the relative error in the following way:

   relative error < 16 errtol^6 / (1 - 2 errtol)

     errtol     precision
     ------     ----------
     0.001       1.0e-17
     0.003       2.0e-14
     0.01        2.0e-11
     0.03        2.0e-8
     0.1         2.0e-5
*/

int gsl_sf_ellint_RD_e(double x, double y, double z, gsl_mode_t mode, gsl_sf_result *result)
{
  const gsl_prec_t goal = GSL_MODE_PREC(mode);
  const double errtol = (goal == GSL_PREC_DOUBLE ? 0.001 : 0.03);
  const double prec = gsl_prec_eps[goal];
  const double lolim = 2.0 / pow(GSL_DBL_MAX, 2.0 / 3.0);
  const double uplim = pow(0.1 * errtol / GSL_DBL_MIN, 2.0 / 3.0);
  const int nmax = 10000;

  if (GSL_MIN(x, y) < 0.0 || GSL_MIN(x + y, z) < lolim)
  {
    DOMAIN_ERROR(result);
  }
  else if (locMAX3(x, y, z) < uplim)
  {
    const double c1 = 3.0 / 14.0;
    const double c2 = 1.0 / 6.0;
    const double c3 = 9.0 / 22.0;
    const double c4 = 3.0 / 26.0;
    double xn = x;
    double yn = y;
    double zn = z;
    double sigma = 0.0;
    double power4 = 1.0;
    double ea, eb, ec, ed, ef, s1, s2;
    double mu, xndev, yndev, zndev;
    int n = 0;
    while (1)
    {
      double xnroot, ynroot, znroot, lamda;
      double epslon;
      mu = (xn + yn + 3.0 * zn) * 0.2;
      xndev = (mu - xn) / mu;
      yndev = (mu - yn) / mu;
      zndev = (mu - zn) / mu;
      epslon = locMAX3(fabs(xndev), fabs(yndev), fabs(zndev));
      if (epslon < errtol)
        break;
      xnroot = sqrt(xn);
      ynroot = sqrt(yn);
      znroot = sqrt(zn);
      lamda = xnroot * (ynroot + znroot) + ynroot * znroot;
      sigma += power4 / (znroot * (zn + lamda));
      power4 *= 0.25;
      xn = (xn + lamda) * 0.25;
      yn = (yn + lamda) * 0.25;
      zn = (zn + lamda) * 0.25;
      n++;
      if (n == nmax)
      {
        MAXITER_ERROR(result);
      }
    }
    ea = xndev * yndev;
    eb = zndev * zndev;
    ec = ea - eb;
    ed = ea - 6.0 * eb;
    ef = ed + ec + ec;
    s1 = ed * (-c1 + 0.25 * c3 * ed - 1.5 * c4 * zndev * ef);
    s2 = zndev * (c2 * ef + zndev * (-c3 * ec + zndev * c4 * ea));
    result->val = 3.0 * sigma + power4 * (1.0 + s1 + s2) / (mu * sqrt(mu));
    result->err = prec * fabs(result->val);
    return GSL_SUCCESS;
  }
  else
  {
    DOMAIN_ERROR(result);
  }
}

int gsl_sf_ellint_RF_e(double x, double y, double z, gsl_mode_t mode, gsl_sf_result *result)
{
  const double lolim = 5.0 * GSL_DBL_MIN;
  const double uplim = 0.2 * GSL_DBL_MAX;
  const gsl_prec_t goal = GSL_MODE_PREC(mode);
  const double errtol = (goal == GSL_PREC_DOUBLE ? 0.001 : 0.03);
  const double prec = gsl_prec_eps[goal];
  const int nmax = 10000;

  if (x < 0.0 || y < 0.0 || z < 0.0)
  {
    DOMAIN_ERROR(result);
  }
  else if (x + y < lolim || x + z < lolim || y + z < lolim)
  {
    DOMAIN_ERROR(result);
  }
  else if (locMAX3(x, y, z) < uplim)
  {
    const double c1 = 1.0 / 24.0;
    const double c2 = 3.0 / 44.0;
    const double c3 = 1.0 / 14.0;
    double xn = x;
    double yn = y;
    double zn = z;
    double mu, xndev, yndev, zndev, e2, e3, s;
    int n = 0;
    while (1)
    {
      double epslon, lamda;
      double xnroot, ynroot, znroot;
      mu = (xn + yn + zn) / 3.0;
      xndev = 2.0 - (mu + xn) / mu;
      yndev = 2.0 - (mu + yn) / mu;
      zndev = 2.0 - (mu + zn) / mu;
      epslon = locMAX3(fabs(xndev), fabs(yndev), fabs(zndev));
      if (epslon < errtol)
        break;
      xnroot = sqrt(xn);
      ynroot = sqrt(yn);
      znroot = sqrt(zn);
      lamda = xnroot * (ynroot + znroot) + ynroot * znroot;
      xn = (xn + lamda) * 0.25;
      yn = (yn + lamda) * 0.25;
      zn = (zn + lamda) * 0.25;
      n++;
      if (n == nmax)
      {
        MAXITER_ERROR(result);
      }
    }
    e2 = xndev * yndev - zndev * zndev;
    e3 = xndev * yndev * zndev;
    s = 1.0 + (c1 * e2 - 0.1 - c2 * e3) * e2 + c3 * e3;
    result->val = s / sqrt(mu);
    result->err = prec * fabs(result->val);
    return GSL_SUCCESS;
  }
  else
  {
    DOMAIN_ERROR(result);
  }
}

int gsl_sf_ellint_RJ_e(double x, double y, double z, double p, gsl_mode_t mode, gsl_sf_result *result)
{
  const gsl_prec_t goal = GSL_MODE_PREC(mode);
  const double errtol = (goal == GSL_PREC_DOUBLE ? 0.001 : 0.03);
  const double prec = gsl_prec_eps[goal];
  const double lolim = pow(5.0 * GSL_DBL_MIN, 1.0 / 3.0);
  const double uplim = 0.3 * pow(0.2 * GSL_DBL_MAX, 1.0 / 3.0);
  const int nmax = 10000;

  if (x < 0.0 || y < 0.0 || z < 0.0)
  {
    DOMAIN_ERROR(result);
  }
  else if (x + y < lolim || x + z < lolim || y + z < lolim || p < lolim)
  {
    DOMAIN_ERROR(result);
  }
  else if (locMAX4(x, y, z, p) < uplim)
  {
    const double c1 = 3.0 / 14.0;
    const double c2 = 1.0 / 3.0;
    const double c3 = 3.0 / 22.0;
    const double c4 = 3.0 / 26.0;
    double xn = x;
    double yn = y;
    double zn = z;
    double pn = p;
    double sigma = 0.0;
    double power4 = 1.0;
    double mu, xndev, yndev, zndev, pndev;
    double ea, eb, ec, e2, e3, s1, s2, s3;
    int n = 0;
    while (1)
    {
      double xnroot, ynroot, znroot;
      double lamda, alfa, beta;
      double epslon;
      gsl_sf_result rcresult;
      int rcstatus;
      mu = (xn + yn + zn + pn + pn) * 0.2;
      xndev = (mu - xn) / mu;
      yndev = (mu - yn) / mu;
      zndev = (mu - zn) / mu;
      pndev = (mu - pn) / mu;
      epslon = locMAX4(fabs(xndev), fabs(yndev), fabs(zndev), fabs(pndev));
      if (epslon < errtol)
        break;
      xnroot = sqrt(xn);
      ynroot = sqrt(yn);
      znroot = sqrt(zn);
      lamda = xnroot * (ynroot + znroot) + ynroot * znroot;
      alfa = pn * (xnroot + ynroot + znroot) + xnroot * ynroot * znroot;
      alfa = alfa * alfa;
      beta = pn * (pn + lamda) * (pn + lamda);
      rcstatus = gsl_sf_ellint_RC_e(alfa, beta, mode, &rcresult);
      if (rcstatus != GSL_SUCCESS)
      {
        result->val = 0.0;
        result->err = 0.0;
        return rcstatus;
      }
      sigma += power4 * rcresult.val;
      power4 *= 0.25;
      xn = (xn + lamda) * 0.25;
      yn = (yn + lamda) * 0.25;
      zn = (zn + lamda) * 0.25;
      pn = (pn + lamda) * 0.25;
      n++;
      if (n == nmax)
      {
        MAXITER_ERROR(result);
      }
    }

    ea = xndev * (yndev + zndev) + yndev * zndev;
    eb = xndev * yndev * zndev;
    ec = pndev * pndev;
    e2 = ea - 3.0 * ec;
    e3 = eb + 2.0 * pndev * (ea - ec);
    s1 = 1.0 + e2 * (-c1 + 0.75 * c3 * e2 - 1.5 * c4 * e3);
    s2 = eb * (0.5 * c2 + pndev * (-c3 - c3 + pndev * c4));
    s3 = pndev * ea * (c2 - pndev * c3) - c2 * pndev * ec;
    result->val = 3.0 * sigma + power4 * (s1 + s2 + s3) / (mu * sqrt(mu));
    result->err = prec * fabs(result->val);
    return GSL_SUCCESS;
  }
  else
  {
    DOMAIN_ERROR(result);
  }
}

int main()
{

  gsl_mode_t mode = GSL_PREC_DOUBLE; // 精度模式选择

  // printf("Testing gsl_sf_ellint_RD_e function:\n");

  double x = 0.5;
  double y = 0.5;
  double z = 0.5;
  gsl_sf_result result; // 存储计算结果
  int status = 99;
  int count = 5000000;
  while (count--)
  {
    /* code */
    status = gsl_sf_ellint_RD_e(x, y, z, mode, &result);
  }

  if (status == GSL_SUCCESS)
  {
    printf("%.13f\n", result.val);
  }
  else
  {
    printf("Error: gsl_sf_ellint_RD_e(x=%.6f, y=%.6f, z=%.6f) failed with status %d\n",
           x, y, z, status);
  }

  return 0;
}
