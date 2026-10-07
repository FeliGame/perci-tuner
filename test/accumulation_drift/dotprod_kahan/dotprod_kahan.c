// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* dotprod_kahan.c -- Kahan-compensated dot of sin/cos sequences
 * Family : dot product/reduction
 * Hazards: compensation in inner loop
 *
 * Tunable variables (candidates for float<->double): double dot, c, xi, yi, y, t
 *
 * Build:  make dotprod_kahan
 * Run:    ./dotprod_kahan
 * Oracle: oracles/dotprod_kahan.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int n) {
    double dot = 0.0, c = 0.0;
    for (int i = 0; i < n; i++) {
        double xi = sin((double)i * 1e-3);
        double yi = cos((double)i * 1e-3);
        double y = xi*yi - c;
        double t = dot + y;
        c = (t - dot) - y;
        dot = t;
    }
    return dot;
}

int main(int argc, char **argv) {
    int n = (argc > 1) ? atoi(argv[1]) : 200000;
    printf("%.17g\n", kernel(n));
    return 0;
}
