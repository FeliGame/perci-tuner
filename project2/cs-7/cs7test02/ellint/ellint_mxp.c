/* specfunc/ellint.c
 */

#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_precision.h>
#include <gsl/gsl_sf_ellint.h>
#include "error.h"

/*-*-*-*-*-*-*-*-*-*-*-* Private Section *-*-*-*-*-*-*-*-*-*-*-*/

inline static double locMAX3(double x, double y, double z)
{
  double xy = GSL_MAX(x, y);
  return GSL_MAX(xy, z);
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
  gsl_prec_t goal = GSL_MODE_PREC(mode);
  double errtol = (goal == GSL_PREC_DOUBLE ? 0.001 : 0.03);
  double prec = gsl_prec_eps[goal];
  double lolim = 2.0 / pow(GSL_DBL_MAX, 2.0 / 3.0);
  double uplim = powf(0.1 * errtol / GSL_DBL_MIN, 2.0 / 3.0);
  const int nmax = 10000;

  if (GSL_MIN(x, y) < 0.0 || GSL_MIN(x + y, z) < lolim)
  {
    DOMAIN_ERROR(result);
  }
  else if (locMAX3(x, y, z) < uplim)
  {
    double c1 = 3.0 / 14.0;
    double c2 = 1.0 / 6.0;
    double c3 = 9.0 / 22.0;
    double c4 = 3.0 / 26.0;
    double xn = x;
    double yn = y;
    double zn = z;
    double sigma = 0.0;
    double power4 = 1.0;
    double ea;
    double eb;
    double ec;
    double ed;
    double ef;
    double s1;
    double s2;
    double mu;
    double xndev;
    double yndev;
    double zndev;
    int n = 0;
    while (1)
    {
      double xnroot;
      double ynroot;
      double znroot;
      double lamda;
      double epslon;
      mu = (xn + yn + 3.0 * zn) * 0.2;
      xndev = (mu - xn) / mu;
      yndev = (mu - yn) / mu;
      zndev = (mu - zn) / mu;
      epslon = locMAX3(fabs(xndev), fabs(yndev), fabs(zndev));
      if (epslon < errtol)
        break;
      xnroot = sqrtf(xn);
      ynroot = sqrtf(yn);
      znroot = sqrtf(zn);
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
    result->val = 3.0 * sigma + power4 * (1.0 + s1 + s2) / (mu * sqrtf(mu));
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
  gsl_mode_t mode = GSL_PREC_DOUBLE;

  double x = 0.5;
  double y = 0.5;
  double z = 0.5;
  gsl_sf_result result;
  int status = 99;
  int count = 1000000;
  while (count--)
  {
    status = gsl_sf_ellint_RD_e(x, y, z, mode, &result);
  }

  if (status == GSL_SUCCESS)
    printf("%.13e\n", result.val);
  else
    printf("failed with status %d\n", status);

  return 0;
}