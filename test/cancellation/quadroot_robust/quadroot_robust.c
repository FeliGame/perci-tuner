// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* quadroot_robust.c -- Robust quadratic formula via Vieta
 * Family : root finding
 * Hazards: no cancellation: r2 = c / r1
 *
 * Tunable variables (candidates for float<->double): double d, r1, r2
 *
 * Build:  make quadroot_robust
 * Run:    ./quadroot_robust
 * Oracle: oracles/quadroot_robust.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(double b, double c) {
    double d  = sqrt(b*b - 4.0*c);
    double r1 = (b + d) * 0.5;
    double r2 = c / r1;              /* avoid cancellation */
    return r2;
}

int main(void) {
    printf("%.17g\n", kernel(1e6, 1.0));
    return 0;
}
