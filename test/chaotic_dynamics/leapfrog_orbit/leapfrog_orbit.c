// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* leapfrog_orbit.c -- 2-body Kepler orbit, leapfrog integrator (symplectic)
 * Family : physics/symplectic
 * Hazards: energy invariant; rounding makes orbit precess
 *
 * Tunable variables (candidates for float<->double): double x, y, vx, vy
 *
 * Build:  make leapfrog_orbit
 * Run:    ./leapfrog_orbit
 * Oracle: oracles/leapfrog_orbit.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


double kernel(int steps, double dt) {
    double x = 1.0, y = 0.0, vx = 0.0, vy = 1.0;
    double r  = sqrt(x*x + y*y);
    double inv = 1.0/(r*r*r);
    vx -= 0.5*dt*x*inv;
    vy -= 0.5*dt*y*inv;
    for (int t = 0; t < steps; t++) {
        x += dt*vx; y += dt*vy;
        r  = sqrt(x*x + y*y);
        inv = 1.0/(r*r*r);
        vx -= dt*x*inv; vy -= dt*y*inv;
    }
    return sqrt(x*x + y*y);
}

int main(void) {
    printf("%.17g\n", kernel(100000, 1e-3));
    return 0;
}
