// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* arclen_polar.c -- Arc length of cardioid r(θ)=1-cos(θ)
 * Family : geometry/integration
 * Hazards: r→0 cancellation near θ=0
 *
 * Tunable variables (candidates for float<->double): double h, s
 *
 * Build:  make arclen_polar
 * Run:    ./arclen_polar
 * Oracle: oracles/arclen_polar.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double r(double t)  { return 1.0 - cos(t); }
static double rp(double t) { return sin(t); }
static double g(double t)  { double R=r(t), Rp=rp(t); return sqrt(R*R + Rp*Rp); }

double kernel(double a, double b, int n) {
    double h = (b-a)/n;
    double s = g(a) + g(b);
    for (int i = 1; i < n; i++)
        s += (i & 1) ? 4.0*g(a+i*h) : 2.0*g(a+i*h);
    return s * h / 3.0;
}

int main(void) {
    printf("%.17g\n", kernel(0.0, 2.0*M_PI, 20000));
    return 0;
}
