// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

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
    double result = 0;
    int count = 10000000;
    while (count--)
    {
        result = ex0(c1);
    }
    printf("%.13e\n", result);
    return 0;
}