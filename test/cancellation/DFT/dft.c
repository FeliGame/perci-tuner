// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize "%s" -arg=1 -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define PI 3.14159265358979323846

#define N 5000

double in_real[N];
double in_imag[N];
double out_real[N];
double out_imag[N];

void dft()
{
    double W_real[N];
    double W_imag[N];
    double arg = 2.0 * PI / (double)N;

    /* Generation of coefficients W */
    for (int i = 0; i < N; i++) {
        W_real[i] =  cos(arg * (double)i);
        W_imag[i] = -sin(arg * (double)i);
    }

    /* The main computation kernel */
    for (int k = 0; k < N; k++) {
        out_real[k] = in_real[k];
        out_imag[k] = in_imag[k];
        for (int n = 0; n < N; n++) {
            int p = (n * k) % N;
            out_real[k] = out_real[k] + in_real[n] * W_real[p] - in_imag[n] * W_imag[p];
            out_imag[k] = out_imag[k] + in_real[n] * W_imag[p] + in_imag[n] * W_real[p];
        }
    }
}

double sgn(const double x)
{
    // return -1 if x is negative, 0 if it is zero and 1 if it is positive
    // NOTE: this is not differentiable; it is only used to generate inputs!
    return ((x > 0.0) - (x < 0.0));
}

int main(int argc, char* argv[])
{
    int numRuns = 5;  // default repeat count to increase runtime

    if (argc == 2) {
        numRuns = atoi(argv[1]);
        if (numRuns < 1) {
            numRuns = 1;
        }
    }

    /* Generate inputs */
    for (int i = 0; i < N; i++) {
        // scale i to [0,2*pi)
        double x = ((2.0*PI) / (double)N) * (double)i;

        // minor variation on a square wave (the classic FFT encoding example)
        in_real[i] = sgn(sin(x+1.0))+cos(x);
        in_imag[i] = sgn(cos(x+1.0))+sin(x);
    }
    /* Run discrete fourier transform routine multiple times */
    for (int run = 0; run < numRuns; run++) {
        dft();
    }

    /* Calculate and print output norm (single core floating-point result) */
    double norm = 0.0;
    for (int i = 0; i < N; i++) {
        norm += out_real[i] * out_real[i];
    }
    norm = sqrt(norm);
    printf("%.13e\n", norm);

    return 0;
}