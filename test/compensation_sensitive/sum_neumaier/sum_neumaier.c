// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* sum_neumaier.c -- Neumaier-improved Kahan harmonic summation
 * Family : summation/reduction
 * Hazards: branch on |s| vs |x|, sign-sensitive compensation
 *
 * Tunable variables (candidates for float<->double): double s, c, t, x
 *
 * Build:  make sum_neumaier
 * Run:    ./sum_neumaier
 * Oracle: oracles/sum_neumaier.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int n) {
    double s = 1.0, c = 0.0;
    for (int i = 2; i <= n; i++) {
        double x = 1.0 / (double)i;
        double t = s + x;
        if (fabs(s) >= fabs(x)) c += (s - t) + x;
        else                    c += (x - t) + s;
        s = t;
    }
    return s + c;
}

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 1000000;
    printf("%.17g\n", kernel(n));
    return 0;
}
