// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* erf_taylor.c -- erf via Taylor series, summed over a grid
 * Family : special functions
 * Hazards: alternating-sign series
 *
 * Tunable variables (candidates for float<->double): double sum, term
 *
 * Build:  make erf_taylor
 * Run:    ./erf_taylor
 * Oracle: oracles/erf_taylor.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double erf_taylor(double x) {
    double s = 0.0, term = x;
    for (int k = 0; k < 100; k++) {
        s += term / (double)(2*k + 1);
        if (k < 99) term *= -x*x / (double)(k+1);
    }
    return s * 2.0 / sqrt(M_PI);
}

double kernel(int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += erf_taylor(3.0 * (double)i / (double)n);
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(200));
    return 0;
}
