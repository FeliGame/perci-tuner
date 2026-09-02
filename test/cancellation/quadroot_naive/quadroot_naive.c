// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* quadroot_naive.c -- Naive quadratic formula for x^2 - bx + c = 0  (b=1e6, c=1)
 * Family : root finding
 * Hazards: catastrophic cancellation in (b - sqrt(b^2-4c))
 *
 * Tunable variables (candidates for float<->double): double d, r1, r2
 *
 * Build:  make quadroot_naive
 * Run:    ./quadroot_naive
 * Oracle: oracles/quadroot_naive.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(double b, double c) {
    double d = sqrt(b*b - 4.0*c);
    double r2 = (b - d) * 0.5;       /* small-root branch: cancellation */
    return r2;
}

int main(void) {
    printf("%.17g\n", kernel(1e6, 1.0));
    return 0;
}
