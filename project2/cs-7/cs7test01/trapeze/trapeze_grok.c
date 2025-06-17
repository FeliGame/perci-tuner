#include <math.h>
#include <stdio.h>

double ex0(double u)
{
    double a = 0.25;
    double b = 5000.0;
    double n = 25.0;
    float h = (float)((b - a) / n);
    double r = 0.0;
    float xa = 0.25f;
    while (xa < 5000.0f)
    {
        float v = xa + h;
        float xb = (v > 5000.0f) ? 5000.0f : v;
        double gxa = u / ((((((0.7 * (double)xa) * (double)xa) * (double)xa) - ((0.6 * (double)xa) * (double)xa)) + (0.9 * (double)xa)) - 0.2);
        double gxb = u / ((((((0.7 * (double)xb) * (double)xb) * (double)xb) - ((0.6 * (double)xb) * (double)xb)) + (0.9 * (double)xb)) - 0.2);
        r += ((gxa + gxb) * 0.5) * (double)h;
        xa += h;
    }
    return r;
}

int main()
{
    double u = 1.0;
    double result = 0;
    int count = 500000;
    while (count--)
    {
        result = ex0(u);
    }
    printf("%.13e\n", result);
    return 0;
}