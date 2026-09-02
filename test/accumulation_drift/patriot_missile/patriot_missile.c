// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* patriot_missile.c -- 0.1 accumulated N times, then *10 (Patriot timing bug)
 * Family : classic hazards
 * Hazards: 0.1 not exact in binary => linear drift
 *
 * Tunable variables (candidates for float<->double): double t
 *
 * Build:  make patriot_missile
 * Run:    ./patriot_missile
 * Oracle: oracles/patriot_missile.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int N) {
    double t = 0.0;
    for (int i = 0; i < N; i++) t += 0.1;
    return t * 10.0;     /* should equal N */
}

int main(void) {
    int N = 36000;
    printf("%.17g\n", kernel(N));
    return 0;
}
