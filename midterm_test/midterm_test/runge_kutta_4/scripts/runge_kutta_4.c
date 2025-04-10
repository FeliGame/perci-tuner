#include <stdio.h>
#include <math.h>

double ex0(double h, double y_n_42_, double c)
{
    double sixieme = 1.0 / 6.0;
    double eps = 0.005;
    double k = 1.2;
    double y_n = y_n_42_;
    double e = 1.0;
    while (e > eps)
    { 
        double v = c - y_n;
        double k1 = (k * v) * v;
        double v_1 = c - (y_n + (0.5 * h) * k1);
        double k2 = (k * v_1) * v_1;
        double v_2 = c - (y_n + (0.5 * h) * k2);
        double k3 = (k * v_2) * v_2;
        double v_3 = c - (y_n + h * k3);
        double k4 = (k * v_3) * v_3;
        y_n += (sixieme * h) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
        e -= eps;
    }
    return fabs(e);
}

int main()
{
    double h = 0.1;
    double y_n_42_ = 1.0;
    double c = 2.4;
    double result = ex0(h, y_n_42_, c);
    printf("%.13e\n", result);
    return 0;
}