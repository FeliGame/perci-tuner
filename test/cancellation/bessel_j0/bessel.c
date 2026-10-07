// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

#include <float.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880168872420969808
#endif

typedef double pb_real;

typedef struct {
  pb_real val;
  pb_real err;
} precbench_sf_result;

enum { PRECBENCH_SUCCESS = 0, PRECBENCH_EDOM = 1 };

static int precbench_bessel_cos_pi4_e(pb_real y, pb_real eps, precbench_sf_result *result)
{
  const pb_real sy = sin(y);
  const pb_real cy = cos(y);
  const pb_real s = sy + cy;
  const pb_real d = sy - cy;
  const pb_real abs_sum = fabs(cy) + fabs(sy);
  pb_real seps;
  pb_real ceps;

  if (fabs(eps) < (pb_real)1.0e-3) {
    const pb_real e2 = eps * eps;
    seps = eps * (1.0 - e2 / 6.0 * (1.0 - e2 / 20.0));
    ceps = 1.0 - e2 / 2.0 * (1.0 - e2 / 12.0);
  } else {
    seps = sin(eps);
    ceps = cos(eps);
  }

  result->val = (ceps * s - seps * d) / (pb_real)M_SQRT2;
  result->err = 2.0 * (pb_real)DBL_EPSILON * (fabs(ceps) + fabs(seps)) * abs_sum / (pb_real)M_SQRT2;
  return PRECBENCH_SUCCESS;
}

int precbench_bessel_J0_e(pb_real x, precbench_sf_result *result)
{
  const pb_real y = fabs(x);

  if (y < 2.0 * sqrt((pb_real)DBL_EPSILON)) {
    result->val = 1.0;
    result->err = y * y;
    return PRECBENCH_SUCCESS;
  }

  /*
   * Self-contained PrecBench variant of the large-argument J0 path.
   * It deliberately keeps the phase computation with sy +/- cy so the
   * benchmark continues to expose cancellation near phase boundaries, while
   * avoiding GSL public API names and GSL-internal coefficient tables.
   */
  precbench_sf_result cp;
  const pb_real theta = y - (pb_real)M_PI / 4.0;
  const int stat_cp = precbench_bessel_cos_pi4_e(theta, 0.0, &cp);
  const pb_real ampl = sqrt(2.0 / ((pb_real)M_PI * y));
  result->val = ampl * cp.val;
  result->err = fabs(ampl) * cp.err + (pb_real)DBL_EPSILON * fabs(result->val);
  return stat_cp;
}

pb_real precbench_bessel_J0(pb_real x)
{
  precbench_sf_result result;
  int status = precbench_bessel_J0_e(x, &result);
  return status == PRECBENCH_SUCCESS ? result.val : (pb_real)NAN;
}

int main(void)
{
  /*
   * Exercise points close to zeros of cos(x - pi/4).  Around these phase
   * boundaries sy + cy is small, so lowering precision in the phase path can
   * change the sign or magnitude of the final asymptotic term.
   */
  const pb_real offsets[] = {
      -1.0e-10, -1.0e-12, -1.0e-14, 0.0,
       1.0e-14,  1.0e-12,  1.0e-10};
  const int n_offsets = (int)(sizeof(offsets) / sizeof(offsets[0]));
  pb_real checksum = 0.0;

  for (int repeat = 0; repeat < 20000; ++repeat) {
    for (int k = 2; k <= 8; ++k) {
      const pb_real phase_zero = (pb_real)M_PI * ((pb_real)k + 0.75);
      for (int i = 0; i < n_offsets; ++i) {
        const pb_real x = phase_zero + offsets[i];
        const pb_real weight = 1.0 + (pb_real)(k - 2) * 0.125 + (pb_real)i * 0.015625;
        checksum += weight * precbench_bessel_J0(x);
      }
    }
  }

  printf("%.17e\n", checksum);
  return 0;
}
