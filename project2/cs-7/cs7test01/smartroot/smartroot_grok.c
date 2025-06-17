#include <math.h>
#include <stdio.h>

double ex0(double c)
{
    float a = 3.0f;
    float b = 6.0f;
    double discr = ((double)(b * b) - ((double)a * c) * 4.0f);
    double tmp_1;
    if (discr > 10.0f)
    {
        if (b > 0.0f)
        {
            tmp_1 = (c * 2.0f) / (-(double)b - (double)sqrtf((float)discr));
        }
        else
        {
            tmp_1 = (-(double)b + (double)sqrtf((float)discr)) / ((double)a * 2.0f);
        }
    }
    else
    {
        tmp_1 = (-(double)b + (double)sqrtf((float)discr)) / ((double)a * 2.0f);
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