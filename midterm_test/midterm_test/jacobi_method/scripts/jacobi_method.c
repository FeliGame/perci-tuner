#include <stdio.h>
#include <math.h>

double ex0(double a11, double a22, double a33, double a44, double b1, double b2, double b3, double b4) {
    double eps = 1e-17;
    double x_n1 = 0.0;
    double x_n2 = 0.0;
    double x_n3 = 0.0;
    double x_n4 = 0.0;
    double e = 1.0;
    double x1 = 0.0;
    double x2 = 0.0;
    double x3 = 0.0;
    double x4 = 0.0;
    while (e > eps) {
        x_n1 = (b1 / a11) - (0.1 / a11) * x2 - (0.2 / a11) * x3 + (0.3 / a11) * x4;
        x_n2 = (b2 / a22) - (0.3 / a22) * x1 + (0.1 / a22) * x3 - (0.2 / a22) * x4;
        x_n3 = (b3 / a33) - (0.2 / a33) * x1 + (0.3 / a33) * x2 - (0.1 / a33) * x4;
        x_n4 = (b4 / a44) + (0.1 / a44) * x1 - (0.2 / a44) * x2 - (0.3 / a44) * x3;
        e = fabs(x_n2 - x2);
        x1 = x_n1;
        x2 = x_n2;
        x3 = x_n3;
        x4 = x_n4;
    }
    return x2;
}

int main() {
    double a11 = 10.0, a22 = 20.0, a33 = 30.0, a44 = 40.0;
    double b1 = 1.0, b2 = 2.0, b3 = 3.0, b4 = 4.0;
    double result = ex0(a11, a22, a33, a44, b1, b2, b3, b4);
    printf("%.13e\n", result);
    return 0;
}