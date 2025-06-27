// This src tests all features of Perci-Tuner
#include <stdio.h>

long double precision_test()
{
    long double a_val = 2.0 + 1e-5L, b_val = 2.0L;
    long double ret;
// #pragma precision region
    // {
// #pragma precision range(__fp16, float, double, __float128)
        for (int i = 0; i < 100; ++i)        
            ret += (a_val - b_val) * 1e10;
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
        for (int i = 0; i < 100; ++i)
            ret += (*a - *b) * 1e10;
    // }
    return ret;
}

long double array_test()
{
    long double a[100];
    long double b[100];
    long double ret;
#pragma precision region
    {
#pragma precision range(__fp16, float, double, __float128)
        for (int i = 0; i < 100; ++i) {
            a[i] = 2.0L + 1e-20L;
            b[i] = 2.0L;
            ret += (a[i] - b[i]) * 1e10;
        }
    }
    return ret;
}

int main()
{
    long double result = array_test();
#pragma precision error result(1e-5)
    return 0;
}