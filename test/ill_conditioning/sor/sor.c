// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* sor.c -- Successive over-relaxation, omega=1.5
 * Family : linear solver
 * Hazards: weighting factor changes spectral radius
 *
 * Tunable variables (candidates for float<->double): double x[], omega
 *
 * Build:  make sor
 * Run:    ./sor
 * Oracle: oracles/sor.ref  (printed at full %.17g)
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

double kernel(double omega, int maxiter, double tol) {
    double A[N][N], b[N], x[N];
    setup(A, b);
    for (int i = 0; i < N; i++) x[i] = 0.0;
    for (int iter = 0; iter < maxiter; iter++) {
        double maxd = 0.0;
        for (int i = 0; i < N; i++) {
            double s = b[i];
            for (int j = 0; j < N; j++) if (j != i) s -= A[i][j]*x[j];
            double xn = (1.0 - omega)*x[i] + omega * s / A[i][i];
            double d = fabs(xn - x[i]);
            if (d > maxd) maxd = d;
            x[i] = xn;
        }
        if (maxd < tol) break;
    }
    double s = 0.0;
    for (int i = 0; i < N; i++) s += x[i];
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(1.5, 20000, 1e-12));
    return 0;
}
