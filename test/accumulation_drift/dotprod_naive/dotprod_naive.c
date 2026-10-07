// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* dotprod_naive.c -- Naive dot product of sin/cos sequences
 * Family : dot product/reduction
 * Hazards: small-magnitude products near zero crossings, accumulation
 *
 * Tunable variables (candidates for float<->double): double dot, xi, yi
 *
 * Build:  make dotprod_naive
 * Run:    ./dotprod_naive
 * Oracle: oracles/dotprod_naive.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int n) {
    double dot = 0.0;
    for (int i = 0; i < n; i++) {
        double xi = sin((double)i * 1e-3);
        double yi = cos((double)i * 1e-3);
        dot += xi * yi;
    }
    return dot;
}

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 200000;
    printf("%.17g\n", kernel(n));
    return 0;
}
