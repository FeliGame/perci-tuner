#include <stdio.h>

long double foo(long double c1, long double c2)
{
    return c1 + c2;
}

int main()
{
    long double a = 2;
    long double b = 1e-20L;
#pragma AutoMxPrec
{
    a = foo(a, b);
}
printf("%.20Lf\n", a);
    return 0;
}