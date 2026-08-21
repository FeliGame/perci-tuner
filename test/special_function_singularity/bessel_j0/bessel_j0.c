// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* bessel_j0.c -- J_0 via series (|x|<8) and asymptotic (|x|>=8), summed over [0.1, 100]
 * Family : special functions
 * Hazards: branch on |x|; series and asymptotic differ in precision needs
 *
 * Tunable variables (candidates for float<->double): double sum, term, x
 *
 * Build:  make bessel_j0
 * Run:    ./bessel_j0
 * Oracle: oracles/bessel_j0.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double j0_approx(double x) {
    double ax = fabs(x);
    if (ax < 8.0) {
        double y = x*x, term = 1.0, sum = 1.0;
        for (int k = 1; k < 50; k++) {
            term *= -y / (4.0*(double)k*(double)k);
            sum  += term;
            if (fabs(term) < 1e-16*fabs(sum)) break;
        }
        return sum;
    } else {
        double y  = 8.0/ax, y2 = y*y;
        double p0 = 1.0 - 0.1098628627e-2*y2 + 0.2734510407e-4*y2*y2;
        double q0 = -0.1562499995e-1*y + 0.1430488765e-3*y*y2;
        double phi = ax - 0.785398163397448;
        return sqrt(0.636619772/ax) * (p0*cos(phi) - q0*sin(phi));
    }
}

double kernel(int n) {
    double s = 0.0;
    for (int i = 1; i <= n; i++) s += j0_approx(0.1 * (double)i);
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(1000));
    return 0;
}
