// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* lu_decomp.c -- LU factorization without pivoting (32x32, diagonally dominant)
 * Family : matrix decomposition
 * Hazards: pivot growth in low precision
 *
 * Tunable variables (candidates for float<->double): double A[][]
 *
 * Build:  make lu_decomp
 * Run:    ./lu_decomp
 * Oracle: oracles/lu_decomp.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#define M 32
double kernel(void) {
    double A[M][M];
    for (int i = 0; i < M; i++)
        for (int j = 0; j < M; j++)
            A[i][j] = (i==j) ? ((double)M + 1.0)
                             : 1.0/(1.0 + fabs((double)(i-j)));
    for (int k = 0; k < M; k++) {
        for (int i = k+1; i < M; i++) {
            A[i][k] /= A[k][k];
            for (int j = k+1; j < M; j++)
                A[i][j] -= A[i][k]*A[k][j];
        }
    }
    double det = 1.0;
    for (int i = 0; i < M; i++) det *= A[i][i];
    return det;
}

int main(void) {
    printf("%.17g\n", kernel());
    return 0;
}
