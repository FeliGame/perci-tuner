// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* heat2d.c -- 2D heat equation, explicit finite difference
 * Family : PDE/stencil
 * Hazards: 5-point stencil, time-step rounding
 *
 * Tunable variables (candidates for float<->double): double u[][], r
 *
 * Build:  make heat2d
 * Run:    ./heat2d
 * Oracle: oracles/heat2d.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#define G 32
double kernel(int steps, double dt) {
    double u[G][G], un[G][G];
    for (int i = 0; i < G; i++)
        for (int j = 0; j < G; j++)
            u[i][j] = sin(M_PI*i/G) * sin(M_PI*j/G);
    double h = 1.0/G, alpha = 0.1;
    double r = alpha*dt/(h*h);
    for (int t = 0; t < steps; t++) {
        for (int i = 1; i < G-1; i++)
            for (int j = 1; j < G-1; j++)
                un[i][j] = u[i][j]
                         + r*(u[i+1][j] + u[i-1][j]
                            + u[i][j+1] + u[i][j-1] - 4.0*u[i][j]);
        for (int i = 1; i < G-1; i++)
            for (int j = 1; j < G-1; j++)
                u[i][j] = un[i][j];
    }
    double s = 0.0;
    for (int i = 0; i < G; i++)
        for (int j = 0; j < G; j++) s += u[i][j];
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(2000, 1e-4));
    return 0;
}
