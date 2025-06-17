#include <stdio.h>
#include <math.h>

double ex0(double h, double y_n_42_, double c)
{
    float sixieme = 1.0f / 6.0f;
    float eps = 0.005f;
    float k = 1.2f;
    double y_n = y_n_42_;
    double e = 1.0;
    while (e > (double)eps)
    { 
        float v = (float)(c - y_n);
        float k1 = (k * v) * v;
        float v_1 = (float)(c - (y_n + (double)(0.5f * h) * (double)k1));
        float k2 = (k * v_1) * v_1;
        float v_2 = (float)(c - (y_n + (double)(0.5f * h) * (double)k2));
        float k3 = (k * v_2) * v_2;
        float v_3 = (float)(c - (y_n + (double)h * (double)k3));
        float k4 = (k * v_3) * v_3;
        y_n += (double)(sixieme * h) * ((double)k1 + 2.0f * (double)k2 + 2.0f * (double)k3 + (double)k4);
        e -= (double)eps;
    }
    return fabsf((float)e);
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