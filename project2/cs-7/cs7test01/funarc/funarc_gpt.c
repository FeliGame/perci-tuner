#include <math.h>
#include <stdio.h>

#define INPUTS 10

// 使用 double 精度即可满足误差要求
double fun(double x)
{
    int k, n = 5;
    double t1 = x;
    double d1 = 1.0;

    for (k = 1; k <= n; k++)
    {
        d1 = 2.0 * d1;
        t1 = t1 + sin(d1 * x) / d1;
    }

    return t1;
}

int main()
{
    int l;

    int i, n;
    int arr[] = {100,
                 200,
                 1000,
                 2000,
                 10000,
                 20000,
                 100000,
                 200000,
                 1000000,
                 2000000};

    double h;                  // 步长：double 精度足够
    double t1, t2;             // 记录 fun 值变化：double
    double dppi;               // π 值：double
    long double s1;           // 单个段的累加：保留 long double
    long double result = 0.0L; // 总和：long double，累加精度关键

    for (l = 0; l < INPUTS; l++)
    {
        n = arr[l];
        dppi = acos(-1.0);  // π
        h = dppi / n;       // 步长
        s1 = 0.0L;
        t1 = 0.0;

        for (i = 1; i <= n; i++)
        {
            t2 = fun(i * h);
            s1 += sqrt(h * h + (t2 - t1) * (t2 - t1));
            t1 = t2;
        }

        result += s1;
    }

    printf("%.13Lf\n", result); // 保留 long double 输出

    return 0;
}
