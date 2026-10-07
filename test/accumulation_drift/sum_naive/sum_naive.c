// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* sum_naive.c -- Forward harmonic-series summation (naive)
 * Family : summation/reduction
 * Hazards: linear error accumulation, ULP drift
 *
 * Tunable variables (candidates for float<->double): double s
 *
 * Build:  make sum_naive
 * Run:    ./sum_naive
 * Oracle: oracles/sum_naive.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int n) {
    double s = 0.0;
    for (int i = 1; i <= n; i++) s += 1.0 / (double)i;
    return s;
}

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 1000000;
    printf("%.17g\n", kernel(n));
    return 0;
}
