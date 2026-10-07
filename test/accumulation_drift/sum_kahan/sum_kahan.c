// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* sum_kahan.c -- Kahan compensated harmonic summation
 * Family : summation/reduction
 * Hazards: low-bit compensation; lowering precision destroys compensation
 *
 * Tunable variables (candidates for float<->double): double s, c, y, t
 *
 * Build:  make sum_kahan
 * Run:    ./sum_kahan
 * Oracle: oracles/sum_kahan.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int n) {
    double s = 0.0, c = 0.0;
    for (int i = 1; i <= n; i++) {
        double y = 1.0 / (double)i - c;
        double t = s + y;
        c = (t - s) - y;
        s = t;
    }
    return s;
}

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 1000000;
    printf("%.17g\n", kernel(n));
    return 0;
}
