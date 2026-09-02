// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* nbody.c -- 4-body N-body, symplectic Euler, gravitational softening
 * Family : physics/N-body
 * Hazards: 1/r^3 magnitude range, energy drift
 *
 * Tunable variables (candidates for float<->double): double pos[], vel[], acc[]
 *
 * Build:  make nbody
 * Run:    ./nbody
 * Oracle: oracles/nbody.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#define NB 4
double kernel(int steps, double dt) {
    double m[NB] = {1.0, 1e-3, 1e-3, 1e-3};
    double pos[NB][3] = {{0,0,0},{1,0,0},{0,1.5,0},{-1,-1,0.5}};
    double vel[NB][3] = {{0,0,0},{0,1.0,0.1},{-0.8,0,0},{0.5,0.4,0}};
    double acc[NB][3];
    for (int t = 0; t < steps; t++) {
        for (int i = 0; i < NB; i++)
            acc[i][0] = acc[i][1] = acc[i][2] = 0.0;
        for (int i = 0; i < NB; i++)
            for (int j = i+1; j < NB; j++) {
                double dx = pos[j][0]-pos[i][0];
                double dy = pos[j][1]-pos[i][1];
                double dz = pos[j][2]-pos[i][2];
                double r2 = dx*dx + dy*dy + dz*dz + 1e-6;
                double inv = 1.0/(r2*sqrt(r2));
                acc[i][0] += m[j]*inv*dx;
                acc[i][1] += m[j]*inv*dy;
                acc[i][2] += m[j]*inv*dz;
                acc[j][0] -= m[i]*inv*dx;
                acc[j][1] -= m[i]*inv*dy;
                acc[j][2] -= m[i]*inv*dz;
            }
        for (int i = 0; i < NB; i++)
            for (int k = 0; k < 3; k++) {
                vel[i][k] += dt*acc[i][k];
                pos[i][k] += dt*vel[i][k];
            }
    }
    double s = 0.0;
    for (int i = 0; i < NB; i++)
        for (int k = 0; k < 3; k++) s += pos[i][k]*pos[i][k];
    return sqrt(s);
}

int main(void) {
    printf("%.17g\n", kernel(2000, 1e-3));
    return 0;
}
