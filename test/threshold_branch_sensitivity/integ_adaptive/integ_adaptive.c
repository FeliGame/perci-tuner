// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* integ_adaptive.c -- Adaptive Simpson with local error control
 * Family : numerical integration
 * Hazards: control flow depends on |Sl+Sr-S| comparison (precision-sensitive)
 *
 * Tunable variables (candidates for float<->double): double tol, S, Sl, Sr
 *
 * Build:  make integ_adaptive
 * Run:    ./integ_adaptive
 * Oracle: oracles/integ_adaptive.ref  (printed at full %.17g)
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

static double simp(double a, double b) {
    double m = 0.5*(a+b);
    return (b-a)/6.0 * (f(a) + 4.0*f(m) + f(b));
}

static double recur(double a, double b, double tol, double S, int depth) {
    double m = 0.5*(a+b);
    double Sl = simp(a,m), Sr = simp(m,b);
    if (depth <= 0 || fabs(Sl+Sr-S) < 15.0*tol)
        return Sl + Sr + (Sl + Sr - S) / 15.0;
    return recur(a,m,tol/2,Sl,depth-1) + recur(m,b,tol/2,Sr,depth-1);
}

double kernel(double a, double b, double tol) {
    return recur(a, b, tol, simp(a,b), 30);
}

int main(void) {
    printf("%.17g\n", kernel(0.0, 5.0, 1e-10));
    return 0;
}
