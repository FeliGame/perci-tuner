// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* cg.c -- Conjugate gradient on SPD Poisson system
 * Family : linear solver/Krylov
 * Hazards: alpha = rr / pAp; precision-sensitive denominators
 *
 * Tunable variables (candidates for float<->double): double x[], r[], p[], Ap[], alpha, beta
 *
 * Build:  make cg
 * Run:    ./cg
 * Oracle: oracles/cg.ref  (printed at full %.17g)
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
    double A[N][N], b[N], x[N], r[N], p[N], Ap[N];
    setup(A, b);
    for (int i = 0; i < N; i++) { x[i] = 0.0; r[i] = b[i]; p[i] = b[i]; }
    double rr = 0.0;
    for (int i = 0; i < N; i++) rr += r[i]*r[i];
    for (int iter = 0; iter < maxiter; iter++) {
        for (int i = 0; i < N; i++) {
            double s = 0.0;
            for (int j = 0; j < N; j++) s += A[i][j]*p[j];
            Ap[i] = s;
        }
        double pAp = 0.0;
        for (int i = 0; i < N; i++) pAp += p[i]*Ap[i];
        double alpha = rr / pAp;
        for (int i = 0; i < N; i++) {
            x[i] += alpha*p[i];
            r[i] -= alpha*Ap[i];
        }
        double rrn = 0.0;
        for (int i = 0; i < N; i++) rrn += r[i]*r[i];
        if (sqrt(rrn) < tol) { rr = rrn; break; }
        double beta = rrn / rr;
        for (int i = 0; i < N; i++) p[i] = r[i] + beta*p[i];
        rr = rrn;
    }
    double s = 0.0;
    for (int i = 0; i < N; i++) s += x[i];
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(N, 1e-12));
    return 0;
}
