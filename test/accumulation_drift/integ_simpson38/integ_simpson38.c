// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* integ_simpson38.c -- Composite Simpson 3/8 rule
 * Family : numerical integration
 * Hazards: weight pattern 3,3,2,3,3,2,...
 *
 * Tunable variables (candidates for float<->double): double h, s
 *
 * Build:  make integ_simpson38
 * Run:    ./integ_simpson38
 * Oracle: oracles/integ_simpson38.ref  (printed at full %.17g)
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
    while (n % 3) n++;                   /* enforce multiple of 3 */
    double h = (b - a) / n;
    double s = f(a) + f(b);
    for (int i = 1; i < n; i++)
        s += (i % 3 == 0) ? 2.0 * f(a + i*h) : 3.0 * f(a + i*h);
    return s * h * 3.0 / 8.0;
}

int main(void) {
    printf("%.17g\n", kernel(0.0, 5.0, 99999));
    return 0;
}
