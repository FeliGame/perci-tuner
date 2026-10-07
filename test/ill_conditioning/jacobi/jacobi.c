// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* jacobi.c -- Jacobi iteration for Poisson tridiagonal system
 * Family : linear solver
 * Hazards: fixed-point convergence, residual
 *
 * Tunable variables (candidates for float<->double): double x[], x_new[], residual
 *
 * Build:  make jacobi
 * Run:    ./jacobi
 * Oracle: oracles/jacobi.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#define N 64
static void setup(double A[N][N], double b[N]) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) A[i][j] = 0.0;
        A[i][i] = 2.0;
        if (i > 0)   A[i][i-1] = -1.0;
        if (i < N-1) A[i][i+1] = -1.0;
        b[i] = sin(M_PI * (double)(i+1) / (double)(N+1));
    }
}

double kernel(int maxiter, double tol) {
    double A[N][N], b[N], x[N], xn[N];
    setup(A, b);
    for (int i = 0; i < N; i++) x[i] = 0.0;
    for (int iter = 0; iter < maxiter; iter++) {
        double maxd = 0.0;
        for (int i = 0; i < N; i++) {
            double s = b[i];
            for (int j = 0; j < N; j++) if (j != i) s -= A[i][j]*x[j];
            xn[i] = s / A[i][i];
        }
        for (int i = 0; i < N; i++) {
            double d = fabs(xn[i] - x[i]);
            if (d > maxd) maxd = d;
            x[i] = xn[i];
        }
        if (maxd < tol) break;
    }
    double s = 0.0;
    for (int i = 0; i < N; i++) s += x[i];
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(20000, 1e-12));
    return 0;
}
