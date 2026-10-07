// RUN: timeout -k 2 200 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

#include <float.h>
#include <math.h>
#include <stdio.h>

typedef double pb_real;

typedef struct {
  pb_real val;
  pb_real err;
} precbench_sf_result;

enum {
  PRECBENCH_SUCCESS = 0,
  PRECBENCH_EDOM = 1,
  PRECBENCH_EMAXITER = 2
};

static inline pb_real precbench_max(pb_real x, pb_real y) { return x > y ? x : y; }
static inline pb_real precbench_min(pb_real x, pb_real y) { return x < y ? x : y; }
static inline pb_real precbench_max3(pb_real x, pb_real y, pb_real z)
{
  return precbench_max(precbench_max(x, y), z);
}

static int precbench_domain_error(precbench_sf_result *result)
{
  result->val = (pb_real)NAN;
  result->err = (pb_real)NAN;
  return PRECBENCH_EDOM;
}

static int precbench_maxiter_error(precbench_sf_result *result)
{
  result->val = (pb_real)NAN;
  result->err = (pb_real)NAN;
  return PRECBENCH_EMAXITER;
}

int precbench_ellint_RD_e(pb_real x, pb_real y, pb_real z, precbench_sf_result *result)
{
  const pb_real errtol = 0.001;
  const pb_real prec = (pb_real)DBL_EPSILON;
  const pb_real lolim = 2.0 / pow((pb_real)DBL_MAX, 2.0 / 3.0);
  const pb_real uplim = pow(0.1 * errtol / (pb_real)DBL_MIN, 2.0 / 3.0);
  const int nmax = 10000;

  if (precbench_min(x, y) < 0.0 || precbench_min(x + y, z) < lolim) {
    return precbench_domain_error(result);
  } else if (precbench_max3(x, y, z) < uplim) {
    const pb_real c1 = 3.0 / 14.0;
    const pb_real c2 = 1.0 / 6.0;
    const pb_real c3 = 9.0 / 22.0;
    const pb_real c4 = 3.0 / 26.0;
    pb_real xn = x;
    pb_real yn = y;
    pb_real zn = z;
    pb_real sigma = 0.0;
    pb_real power4 = 1.0;
    pb_real ea, eb, ec, ed, ef, s1, s2, mu;
    pb_real xndev, yndev, zndev;
    int n = 0;

    while (1) {
      pb_real xnroot, ynroot, znroot, lamda, epslon;
      mu = (xn + yn + 3.0 * zn) * 0.2;
      xndev = (mu - xn) / mu;
      yndev = (mu - yn) / mu;
      zndev = (mu - zn) / mu;
      epslon = precbench_max3(fabs(xndev), fabs(yndev), fabs(zndev));
      if (epslon < errtol) break;
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
      if (n == nmax) return precbench_maxiter_error(result);
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
    return PRECBENCH_SUCCESS;
  }

  return precbench_domain_error(result);
}

int main(void)
{
  pb_real x = 0.5;
  pb_real y = 0.5;
  pb_real z = 0.5;
  precbench_sf_result result;
  int status = 99;
  int count = 1000000;
  while (count--) {
    status = precbench_ellint_RD_e(x, y, z, &result);
  }

  if (status == PRECBENCH_SUCCESS)
    printf("%.13e\n", result.val);
  else
    printf("failed with status %d\n", status);

  return 0;
}
