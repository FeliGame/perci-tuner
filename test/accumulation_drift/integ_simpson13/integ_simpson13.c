// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* integ_simpson13.c -- Composite Simpson 1/3 rule
 * Family : numerical integration
 * Hazards: alternating weights, n must be even
 *
 * Tunable variables (candidates for float<->double): double h, s
 *
 * Build:  make integ_simpson13
 * Run:    ./integ_simpson13
 * Oracle: oracles/integ_simpson13.ref  (printed at full %.17g)
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
    if (n & 1) n++;                      /* enforce even */
    double h = (b - a) / n;
    double s = f(a) + f(b);
    for (int i = 1; i < n; i++)
        s += (i & 1) ? 4.0 * f(a + i*h) : 2.0 * f(a + i*h);
    return s * h / 3.0;
}

int main(void) {
    printf("%.17g\n", kernel(0.0, 5.0, 100000));
    return 0;
}
