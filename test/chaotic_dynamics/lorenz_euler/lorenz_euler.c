// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* lorenz_euler.c -- Lorenz system, forward Euler
 * Family : ODE/chaos
 * Hazards: chaotic + low-order method = catastrophic drift
 *
 * Tunable variables (candidates for float<->double): double x, y, z, dx, dy, dz
 *
 * Build:  make lorenz_euler
 * Run:    ./lorenz_euler
 * Oracle: oracles/lorenz_euler.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static const double SIG = 10.0, RHO = 28.0, BETA = 8.0/3.0;

double kernel(int N, double dt) {
    double x=1.0, y=1.0, z=1.0;
    for (int i = 0; i < N; i++) {
        double dx = SIG*(y-x);
        double dy = x*(RHO-z) - y;
        double dz = x*y - BETA*z;
        x += dt*dx; y += dt*dy; z += dt*dz;
    }
    return x + y + z;
}

int main(void) {
    printf("%.17g\n", kernel(2000, 0.001));
    return 0;
}
