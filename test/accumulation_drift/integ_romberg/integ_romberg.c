// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* integ_romberg.c -- Romberg integration via Richardson extrapolation
 * Family : numerical integration
 * Hazards: subtraction of nearly-equal trapezoid estimates
 *
 * Tunable variables (candidates for float<->double): double R[][]
 *
 * Build:  make integ_romberg
 * Run:    ./integ_romberg
 * Oracle: oracles/integ_romberg.ref  (printed at full %.17g)
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

static double trap(double a, double b, int n) {
    double h = (b - a) / n;
    double s = 0.5 * (f(a) + f(b));
    for (int i = 1; i < n; i++) s += f(a + i*h);
    return s * h;
}

double kernel(double a, double b, int K) {
    double R[16][16];
    for (int k = 0; k < K; k++) R[k][0] = trap(a, b, 1 << k);
    for (int j = 1; j < K; j++)
        for (int k = j; k < K; k++) {
            double p = pow(4.0, j);
            R[k][j] = (p * R[k][j-1] - R[k-1][j-1]) / (p - 1.0);
        }
    return R[K-1][K-1];
}

int main(void) {
    printf("%.17g\n", kernel(0.0, 5.0, 12));
    return 0;
}
