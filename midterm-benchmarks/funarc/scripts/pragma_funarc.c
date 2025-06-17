#include <time.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define INPUTS 10

long double fun(long double x)
{

    int k, n = 5;
    long double t1, d1 = 1.0L;

    t1 = x;

    for (k = 1; k <= n; k++)
    {
#pragma precision error result(1e-6)
#pragma precision region
#pragma precision range(double, float)
        {
            d1 = 2.0 * d1;
            t1 = t1 + sin(d1 * x) / d1;
        }
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
    long double h, t1, t2, dppi;
    // , ans = 5.795776322412856L;
    long double s1;

    // variables for logging/checking
    long double result;

    // dummy calls
    sqrtf(0);
    acosf(0);
    sinf(0);

    for (l = 0; l < INPUTS; l++)
    {
        int n = arr[l];
        t1 = -1.0;
        dppi = acos(t1);
        s1 = 0.0;
        t1 = 0.0;
        h = dppi / n;

        for (i = 1; i <= n; i++)
        {
#pragma precision error result(1e-6)
#pragma precision region
#pragma precision range(double, float)
            {
                t2 = fun(i * h);
                s1 = s1 + sqrt(h * h + (t2 - t1) * (t2 - t1));
                t1 = t2;
            }
        }

        result += s1;
    }

    printf("%.16Le", result);

    return 0;
}