#include <math.h>
#include <stdio.h>

double ex0(double x1, double x2, double x3)
{
    float e1 = (3.0f * ((float)x1 - 0.3689f) * ((float)x1 - 0.3689f)) + 
               (10.0f * ((float)x2 - 0.117f) * ((float)x2 - 0.117f)) + 
               (30.0f * ((float)x3 - 0.2673f) * ((float)x3 - 0.2673f));
    float e2 = (0.1f * ((float)x1 - 0.4699f) * ((float)x1 - 0.4699f)) + 
               (10.0f * ((float)x2 - 0.4387f) * ((float)x2 - 0.4387f)) + 
               (35.0f * ((float)x3 - 0.747f) * ((float)x3 - 0.747f));
    float e3 = (3.0f * ((float)x1 - 0.1091f) * ((float)x1 - 0.1091f)) + 
               (10.0f * ((float)x2 - 0.8732f) * ((float)x2 - 0.8732f)) + 
               (30.0f * ((float)x3 - 0.5547f) * ((float)x3 - 0.5547f));
    float e4 = (0.1f * ((float)x1 - 0.03815f) * ((float)x1 - 0.03815f)) + 
               (10.0f * ((float)x2 - 0.5743f) * ((float)x2 - 0.5743f)) + 
               (35.0f * ((float)x3 - 0.8828f) * ((float)x3 - 0.8828f));
    double exp1 = exp(-(double)e1); // Cast to double for exp
    double exp2 = exp(-(double)e2);
    double exp3 = exp(-(double)e3);
    double exp4 = exp(-(double)e4);
    return -(1.0 * exp1 + 1.2 * exp2 + 3.0 * exp3 + 3.2 * exp4);
}

int main()
{
    double x1 = 0.5;
    double x2 = 0.7;
    double x3 = 0.9;
    double result = 0;
    int count = 5000000;
    while (count--)
    {
        result = ex0(x1, x2, x3);
    }
    printf("%.13e\n", result);
    return 0;
}