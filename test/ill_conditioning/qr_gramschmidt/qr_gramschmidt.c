// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/* qr_gramschmidt.c -- Classical Gram-Schmidt QR on a 32x16 Hilbert-like matrix
 * Family : matrix decomposition
 * Hazards: loss of orthogonality in low precision
 *
 * Tunable variables (candidates for float<->double): double Q[][], R[][]
 *
 * Build:  make qr_gramschmidt
 * Run:    ./qr_gramschmidt
 * Oracle: oracles/qr_gramschmidt.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#define MR 32
#define NC 16
double kernel(void) {
    double A[MR][NC], Q[MR][NC], R[NC][NC];
    for (int i = 0; i < MR; i++)
        for (int j = 0; j < NC; j++)
            A[i][j] = 1.0/((double)(i+j+1));
    for (int j = 0; j < NC; j++) {
        for (int i = 0; i < MR; i++) Q[i][j] = A[i][j];
        for (int k = 0; k < j; k++) {
            double dot = 0.0;
            for (int i = 0; i < MR; i++) dot += Q[i][k]*A[i][j];
            R[k][j] = dot;
            for (int i = 0; i < MR; i++) Q[i][j] -= dot*Q[i][k];
        }
        double norm = 0.0;
        for (int i = 0; i < MR; i++) norm += Q[i][j]*Q[i][j];
        norm = sqrt(norm);
        R[j][j] = norm;
        for (int i = 0; i < MR; i++) Q[i][j] /= norm;
    }
    double prod = 1.0;
    for (int j = 0; j < NC; j++) prod *= R[j][j];
    return prod;
}

int main(void) {
    printf("%.17g\n", kernel());
    return 0;
}
