// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* rump_polynomial.c -- Rump's polynomial f(77617, 33096), true value -0.827396059946821
 * Family : classic hazards
 * Hazards: extreme cancellation between huge intermediates
 *
 * Tunable variables (candidates for float<->double): double a2, b2, b4, b6, b8
 *
 * Build:  make rump_polynomial
 * Run:    ./rump_polynomial
 * Oracle: oracles/rump_polynomial.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double rump(double a, double b) {
    double a2 = a*a;
    double b2 = b*b, b4 = b2*b2, b6 = b4*b2, b8 = b4*b4;
    return (333.75 - a2)*b6
         + a2*(11.0*a2*b2 - 121.0*b4 - 2.0)
         + 5.5*b8
         + a/(2.0*b);
}

double kernel(void) {
    return rump(77617.0, 33096.0);
}

int main(void) {
    printf("%.17g\n", kernel());
    return 0;
}
