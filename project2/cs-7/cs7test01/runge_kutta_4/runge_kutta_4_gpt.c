#include <stdio.h>
#include <math.h>

double ex0(float h, float y_n_42_, float c)
{
    float sixieme = 1.0f / 6.0f;
    float eps = 0.005f;
    float k = 1.2f;
    double y_n = (double)y_n_42_;  // 主状态变量保持 double
    float e = 1.0f;

    while (e > eps)
    {
        double v = (double)(c - y_n);
        double k1 = (k * v) * v;

        double v_1 = (double)(c - (y_n + 0.5 * h * k1));
        double k2 = (k * v_1) * v_1;

        double v_2 = (double)(c - (y_n + 0.5 * h * k2));
        double k3 = (k * v_2) * v_2;

        double v_3 = (double)(c - (y_n + h * k3));
        double k4 = (k * v_3) * v_3;

        y_n += (double)(sixieme * h) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);

        e -= eps;  // 只是计数器，float 足够
    }

    return fabs(e);
}

int main()
{
    float h = 0.1f;
    float y_n_42_ = 1.0f;
    float c = 2.4f;
    double result = 0;
    int count = 50000;
    while (count--)
    {
        result = ex0(h, y_n_42_, c);
    }
    printf("%.13e\n", result);
    return 0;
}
