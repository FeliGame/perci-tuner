#include <math.h>
#include <stdio.h>

double ex0(double u)
{
    double a = 0.25;
    double b = 5000.0;
    double n = 25.0;
    double h = (b - a) / n;
    double r = 0.0;
    double xa = 0.25;
    while (xa < 5000.0)
    {
        double v = xa + h;
        double xb = (v > 5000.0) ? 5000.0 : v;
        double gxa = u / ((((((0.7 * xa) * xa) * xa) - ((0.6 * xa) * xa)) + (0.9 * xa)) - 0.2);
        double gxb = u / ((((((0.7 * xb) * xb) * xb) - ((0.6 * xb) * xb)) + (0.9 * xb)) - 0.2);
        r += ((gxa + gxb) * 0.5) * h;
        xa += h;
    }
    return r;
}

int main()
{
    double u = 1.0;
    double result = ex0(u);
    printf("%.13e\n", result);
    return 0;
}