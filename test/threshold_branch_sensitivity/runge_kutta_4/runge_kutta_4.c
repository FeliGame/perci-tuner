// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

#include <stdio.h>
#include <math.h>

double ex0(double h, double y_n_42_, double c)
{
    double sixieme = 1.0 / 6.0;
    double eps = 0.005;
    double k = 1.2;
    double y_n = y_n_42_;
    int steps = (int)(1.0 / eps + 0.5);
    for (int step = 0; step < steps; step++)
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
    }
    return y_n;
}

int main()
{
    double h = 0.1;
    double y_n_42_ = 1.0;
    double c = 2.4;
    double result = 0;
    int count = 50000;
    while (count--)
    {
        result = ex0(h, y_n_42_, c);
    }
    printf("%.13e\n", result);
    return 0;
}
