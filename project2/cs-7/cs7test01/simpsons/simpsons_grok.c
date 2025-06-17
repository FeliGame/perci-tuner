#include <math.h>
#include <stdio.h>

#define ITERS 1

long double fun(long double x)
{
    float pi = acosf(-1.0f); // Use float
    float result = sinf(pi * (float)x); // Compute in float
    return (long double)result; // Cast to long double
}

int main(int argc, char **argv)
{
    int l;
    int i, j, k;
    const int n = 1000000;
    float a; // Changed to float
    float b; // Changed to float
    double h; // Changed to double
    double s1; // Changed to double
    double x; // Changed to double
    const long double fuzz = 1e-26;

    for (l = 0; l < ITERS; l++)
    {
        a = 0.0f;
        b = 1.0f;
        h = (double)(b - a) / (2.0 * n); // Cast to double
        s1 = 0.0;
        x = a;
        s1 = (double)fun(x); // Cast fun result to double

    L100:
        x = x + h;
        s1 = s1 + 4.0 * (double)fun(x); // Cast fun result to double
        x = x + h;
        if (x + (double)fuzz >= b) // Cast fuzz to double
            goto L110;
        s1 = s1 + 2.0 * (double)fun(x); // Cast fun result to double
        goto L100;

    L110:
        s1 = s1 + (double)fun(x); // Cast fun result to double
    }

    printf("%.16Le\n", (long double)(h * s1 / 3.0)); // Cast final result to long double

    return 0;
}