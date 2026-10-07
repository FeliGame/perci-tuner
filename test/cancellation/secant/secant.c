// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* secant.c -- Secant iteration on f(x)=x^3 - 2x - 5
 * Family : root finding
 * Hazards: f1-f0 cancellation when close to root
 *
 * Tunable variables (candidates for float<->double): double x0, x1, f0, f1
 *
 * Build:  make secant
 * Run:    ./secant
 * Oracle: oracles/secant.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double f(double x) { return x*x*x - 2.0*x - 5.0; }

double kernel(double x0, double x1, double tol, int maxit) {
    double f0 = f(x0), f1 = f(x1);
    for (int i = 0; i < maxit; i++) {
        if (fabs(f1) < tol) break;
        double x2 = x1 - f1 * (x1 - x0) / (f1 - f0);
        x0 = x1; f0 = f1;
        x1 = x2; f1 = f(x1);
    }
    return x1;
}

int main(void) {
    printf("%.17g\n", kernel(2.0, 3.0, 1e-13, 200));
    return 0;
}
