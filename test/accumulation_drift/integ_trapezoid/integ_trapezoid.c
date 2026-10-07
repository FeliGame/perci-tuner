// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* integ_trapezoid.c -- Composite trapezoidal rule on exp(-x^2) cos(x)
 * Family : numerical integration
 * Hazards: O(h^2) endpoint half-weights
 *
 * Tunable variables (candidates for float<->double): double h, s
 *
 * Build:  make integ_trapezoid
 * Run:    ./integ_trapezoid
 * Oracle: oracles/integ_trapezoid.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double f(double x) {
    return exp(-x*x) * cos(x);
}

double kernel(double a, double b, int n) {
    double h = (b - a) / n;
    double s = 0.5 * (f(a) + f(b));
    for (int i = 1; i < n; i++) s += f(a + i*h);
    return s * h;
}

int main(void) {
    printf("%.17g\n", kernel(0.0, 5.0, 100000));
    return 0;
}
