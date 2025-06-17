#include <math.h>
#include <stdio.h>

double ex0(double t0, double w0, double N)
{
    float h = 0.01;
    float L = 2.0;
    float g = 9.80665;
    float t = t0;
    float w = w0;
    float n = 0.0;
    while (n < N)
    {
        float k1w = (-g / L) * sinf(t);
        float k2t = w + (h / 2.0) * k1w;
        float t_1 = t + h * k2t;
        double k2w = (-g / L) * sinf(t + (h / 2.0) * w);
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
    double result = 0;
    int count = 5000;
    while (count--)
    {
        result = ex0(t0, w0, N);
    }
    printf("%.13e\n", result);
    return 0;
}