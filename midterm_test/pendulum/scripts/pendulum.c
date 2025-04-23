#include <math.h>
#include <stdio.h>

double ex0(double t0, double w0, double N)
{
    double h = 0.01;
    double L = 2.0;
    double g = 9.80665;
    double t = t0;
    double w = w0;
    double n = 0.0;
    while (n < N)
    {
        double k1w = (-g / L) * sin(t);
        double k2t = w + (h / 2.0) * k1w;
        double t_1 = t + h * k2t;
        double k2w = (-g / L) * sin(t + (h / 2.0) * w);
        double w_2 = w + h * k2w;
        t = t_1;
        w = w_2;
        n += 1.0;
    }
    return t;
}

int main()
{
    double t0 = 0.0;
    double w0 = 1.0;
    double N = 1000;
    double result = ex0(t0, w0, N);
    printf("%.13e\n", result);
    return 0;
}