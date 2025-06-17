#include <math.h>
#include <stdio.h>

#define INPUTS 10

long double fun(long double x)
{
    int k, n = 5;
    double t1, d1 = 1.0L;

    t1 = x;

    for (k = 1; k <= n; k++)
    {
        d1 = 2.0 * d1;
        t1 = t1 + sinf(d1 * x) / d1;
    }

    return t1;
}

int main()
{
    int l;
    long int diff = 0;

    int i, j, k;
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
    double h, t1, t2, dppi;
    double s1;
    long double result;

    for (l = 0; l < INPUTS; l++)
    {
        int n = arr[l];
        t1 = -1.0;
        dppi = acosf(t1);
        s1 = 0.0;
        t1 = 0.0;
        h = dppi / n;

        for (i = 1; i <= n; i++)
        {
            t2 = fun(i * h);
            s1 = s1 + sqrtf(h * h + (t2 - t1) * (t2 - t1));
            t1 = t2;
        }
        result += s1;
    }

    if (sizeof(typeof(result)) == sizeof(long double))
        printf("%.13Lf\n", result);
    else if (sizeof(typeof(result)) == sizeof(double))
        printf("%.13lf\n", result);
    else if (sizeof(typeof(result)) == sizeof(float))
        printf("%.13f\n", result);

    return 0;
}
