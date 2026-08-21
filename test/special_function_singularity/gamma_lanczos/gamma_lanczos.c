// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/* gamma_lanczos.c -- Gamma function via Lanczos g=7, summed over [0.1, 10]
 * Family : special functions
 * Hazards: rational sum + pow/exp; reflection for x<0.5
 *
 * Tunable variables (candidates for float<->double): double a, t
 *
 * Build:  make gamma_lanczos
 * Run:    ./gamma_lanczos
 * Oracle: oracles/gamma_lanczos.ref  (printed at full %.17g)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static double gamma_lanczos(double x) {
    static const double c[9] = {0.99999999999980993,
        676.5203681218851, -1259.1392167224028, 771.32342877765313,
        -176.61502916214059, 12.507343278686905, -0.13857109526572012,
        9.9843695780195716e-6, 1.5056327351493116e-7};
    if (x < 0.5)
        return M_PI / (sin(M_PI*x) * gamma_lanczos(1.0 - x));
    x -= 1.0;
    double a = c[0];
    for (int i = 1; i < 9; i++) a += c[i] / (x + (double)i);
    double t = x + 7.0 + 0.5;
    return sqrt(2.0*M_PI) * pow(t, x+0.5) * exp(-t) * a;
}

double kernel(int n) {
    double s = 0.0;
    for (int i = 1; i <= n; i++) s += gamma_lanczos(0.1 * (double)i);
    return s;
}

int main(void) {
    printf("%.17g\n", kernel(100));
    return 0;
}
