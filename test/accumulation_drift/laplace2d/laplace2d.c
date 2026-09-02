// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* laplace2d.c -- Laplace equation on unit square via Gauss-Seidel
 * Family : PDE/stencil
 * Hazards: convergence depends on tol; rounding limits attainable residual
 *
 * Tunable variables (candidates for float<->double): double u[][]
 *
 * Build:  make laplace2d
 * Run:    ./laplace2d
 * Oracle: oracles/laplace2d.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#define LG 32
double kernel(int maxiter, double tol) {
    double u[LG][LG];
    for (int i = 0; i < LG; i++)
        for (int j = 0; j < LG; j++) u[i][j] = 0.0;
    for (int j = 0; j < LG; j++) u[0][j] = 1.0;
    double maxd = 1.0;
    int iter = 0;
    while (maxd > tol && iter++ < maxiter) {
        maxd = 0.0;
        for (int i = 1; i < LG-1; i++)
            for (int j = 1; j < LG-1; j++) {
                double old = u[i][j];
                u[i][j] = 0.25*(u[i+1][j] + u[i-1][j]
                              + u[i][j+1] + u[i][j-1]);
                double d = fabs(u[i][j] - old);
                if (d > maxd) maxd = d;
            }
    }
    double s = 0.0;
    for (int i = 0; i < LG; i++)
        for (int j = 0; j < LG; j++) s += u[i][j];
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(50000, 1e-10));
    return 0;
}
