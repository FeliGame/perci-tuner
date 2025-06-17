#include <math.h>
#include <stdio.h>

double ex0(double c)
{
    float a = 3.0f;
    float b = 6.0f;
    double discr = (double)(b * b) - ((double)(a * c) * 4.0);
    double tmp_1;
    if (discr > 10.0)
    {
        if (b > 0.0f)
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
    double result = 0;
    int count = 10000000;
    while (count--)
    {
        result = ex0(c1);
    }
    printf("%.13e\n", result);
    return 0;
}
