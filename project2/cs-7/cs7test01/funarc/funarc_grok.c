#include <math.h>
#include <stdio.h>

#define INPUTS 10

long double fun(double x)
{
    int k, n = 5;
    float t1 = 1.0f; // Changed to float
    float d1 = 1.0f; // Changed to float

    t1 = (float)x;

    for (k = 1; k <= n; k++)
    {
        d1 = 2.0f * d1;
        t1 = t1 + sinf(d1 * (float)x) / d1;
    }

    return (long double)t1; // Cast back to long double
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
    double h; // Changed to double
    double t1; // Changed to double
    double t2; // Changed to double
    double dppi; // Changed to double
    double s1; // Changed to double
    long double result; // Keep as long double

    for (l = 0; l < INPUTS; l++)
    {
        int n = arr[l];
        t1 = -1.0;
        dppi = (double)acos(-1.0); // Changed to double
        s1 = 0.0;
        t1 = 0.0;
        h = dppi / (double)n; // Cast n to double

        for (i = 1; i <= n; i++)
        {
            t2 = (double)fun((double)(i * h)); // Cast to double
            s1 = s1 + sqrt((double)(h * h + (t2 - t1) * (t2 - t1))); // Compute in double
            t1 = t2;
        }

        result += (long double)s1; // Cast to long double
    }

    if (sizeof(result) == sizeof(long double))
        printf("%.13Lf\n", result);
    else if (sizeof(result) == sizeof(double))
        printf("%.13lf\n", result);
    else if (sizeof(result) == sizeof(float))
        printf("%.13f\n", result);

    return 0;
}