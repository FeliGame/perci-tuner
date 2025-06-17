#include <math.h>
#include <stdio.h>

double ex0(double u)
{
    double a = 0.25;
    double b = 5000.0;
    double n = 25.0;
    double h = (b - a) / n;
    double r = 0.0;
    double xa = a;
    while (xa < b)
    {
        double v = xa + h;
        double xb = (v > b) ? b : v;

        // 使用 float 计算多项式分母及中间项
        float xa_f = (float)xa;
        float xb_f = (float)xb;

        float pxa = ((0.7f * xa_f * xa_f * xa_f) - (0.6f * xa_f * xa_f) + (0.9f * xa_f) - 0.2f);
        float pxb = ((0.7f * xb_f * xb_f * xb_f) - (0.6f * xb_f * xb_f) + (0.9f * xb_f) - 0.2f);

        float gxa_f = (float)u / pxa;
        float gxb_f = (float)u / pxb;

        double gxa = (double)gxa_f;
        double gxb = (double)gxb_f;

        r += ((gxa + gxb) * 0.5) * h;
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