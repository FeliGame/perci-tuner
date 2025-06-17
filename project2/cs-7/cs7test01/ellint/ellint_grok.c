#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_precision.h>
#include <gsl/gsl_sf_ellint.h>
#include "error.h"

inline static float locMAX3_float(float x, float y, float z)
{
  float xy = GSL_MAX(x, y);
  return GSL_MAX(xy, z);
}

int gsl_sf_ellint_RD_e(double x, double y, double z, gsl_mode_t mode, gsl_sf_result *result)
{
  const gsl_prec_t goal = GSL_MODE_PREC(mode);
  const float errtol = (goal == GSL_PREC_DOUBLE ? 0.001f : 0.03f);
  const float prec = (float)gsl_prec_eps[goal];
  const float lolim = 2.0f / powf(GSL_DBL_MAX, 2.0f / 3.0f);
  const float uplim = powf(0.1f * errtol / GSL_DBL_MIN, 2.0f / 3.0f);
  const int nmax = 10000;

  if (GSL_MIN(x, y) < 0.0 || GSL_MIN(x + y, z) < (double)lolim)
  {
    DOMAIN_ERROR(result);
  }
  else if (locMAX3_float((float)x, (float)y, (float)z) < uplim)
  {
    const float c1 = 3.0f / 14.0f;
    const float c2 = 1.0f / 6.0f;
    const float c3 = 9.0f / 22.0f;
    const float c4 = 3.0f / 26.0f;
    float xn = (float)x;
    float yn = (float)y;
    float zn = (float)z;
    double sigma = 0.0;
    float power4 = 1.0f;
    float ea;
    float eb;
    float ec;
    float ed;
    float ef;
    float s1;
    float s2;
    double mu;
    double xndev;
    double yndev;
    double zndev;
    int n = 0;
    while (1)
    {
      float xnroot;
      float ynroot;
      float znroot;
      float lamda;
      float epslon;
      mu = ((double)xn + (double)yn + 3.0 * (double)zn) * 0.2;
      xndev = (mu - (double)xn) / mu;
      yndev = (mu - (double)yn) / mu;
      zndev = (mu - (double)zn) / mu;
      epslon = locMAX3_float(fabsf(xndev), fabsf(yndev), fabsf(zndev));
      if (epslon < errtol)
        break;
      xnroot = sqrtf(xn);
      ynroot = sqrtf(yn);
      znroot = sqrtf(zn);
      lamda = xnroot * (ynroot + znroot) + ynroot * znroot;
      sigma += (double)power4 / ((double)znroot * ((double)zn + (double)lamda));
      power4 *= 0.25f;
      xn = ((double)xn + (double)lamda) * 0.25f;
      yn = ((double)yn + (double)lamda) * 0.25f;
      zn = ((double)zn + (double)lamda) * 0.25f;
      n++;
      if (n == nmax)
      {
        MAXITER_ERROR(result);
      }
    }
    ea = (float)xndev * (float)yndev;
    eb = (float)zndev * (float)zndev;
    ec = ea - eb;
    ed = ea - 6.0f * eb;
    ef = ed + ec + ec;
    s1 = ed * (-c1 + 0.25f * c3 * ed - 1.5f * c4 * (float)zndev * ef);
    s2 = (float)zndev * (c2 * ef + (float)zndev * (-c3 * ec + (float)zndev * c4 * ea));
    result->val = 3.0 * sigma + (double)power4 * (1.0f + s1 + s2) / (mu * sqrt(mu));
    result->err = (double)prec * fabs(result->val);
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