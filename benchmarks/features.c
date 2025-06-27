// This src tests all features of Perci-Tuner
#include <stdio.h>

long double precision_test()
{
    long double a_val = 2.0 + 1e-5L, b_val = 2.0L;
    long double ret;
// #pragma precision region
    // {
// #pragma precision range(__fp16, float, double, __float128)
        ret = (a_val - b_val) * 1e10;
    // }
    return ret;
}

long double pointer_test()
{
    long double a_val = 2.0 + 1e-5L, b_val = 2.0L;
    long double *a, *b;
    long double ret;
// #pragma precision region
//     {
// #pragma precision range(__fp16, float, double, __float128)
        a = &a_val;
        b = &b_val;
        ret = (*a - *b) * 1e10;
    // }
    return ret;
}

long double array_test()
{
    long double a[2] = {2.0 + 1e-20L, 3.0 + 1e-20L};
    long double b[2] = {2.0, 3.0};
    long double ret;
#pragma precision region
    {
#pragma precision range(__fp16, float, double, __float128)
        ret = (a[0] - b[0]) * 1e10 + (a[1] - b[1]) * 1e10;
    }
    return ret;
}

int main()
{
    long double result = array_test();
#pragma precision error result(1e-10)
    return 0;
}