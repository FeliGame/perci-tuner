#include <math.h>
#include <stdio.h>
double ex0(double c)
{
    double a = 3.0;
    double b = 6.0;
    double discr = (b * b) - ((a * c) * 4.0);
    double tmp_1;
    if (discr > 10.0)
    {
        if (b > 0.0)
        {
            tmp_1 = (c * 2.0) / (-b - sqrt(discr));
        }
        else
        {
            tmp_1 = (-b + sqrt(discr)) / (a * 2.0);
        }
    }
    else
    {
        tmp_1 = (-b + sqrt(discr)) / (a * 2.0);
    }
    return tmp_1;
}

int main()
{
    double c1 = 2.0;
    double result = ex0(c1);
    printf("%.13e\n", result);
    return 0;
}