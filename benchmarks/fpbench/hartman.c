#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double x1, double x2, double x3)
{
    double e1 = ((3.0 * ((x1 - 0.3689) * (x1 - 0.3689))) + (10.0 * ((x2 - 0.117) * (x2 - 0.117)))) + (30.0 * ((x3 - 0.2673) * (x3 - 0.2673)));
    double e2 = ((0.1 * ((x1 - 0.4699) * (x1 - 0.4699))) + (10.0 * ((x2 - 0.4387) * (x2 - 0.4387)))) + (35.0 * ((x3 - 0.747) * (x3 - 0.747)));
    double e3 = ((3.0 * ((x1 - 0.1091) * (x1 - 0.1091))) + (10.0 * ((x2 - 0.8732) * (x2 - 0.8732)))) + (30.0 * ((x3 - 0.5547) * (x3 - 0.5547)));
    double e4 = ((0.1 * ((x1 - 0.03815) * (x1 - 0.03815))) + (10.0 * ((x2 - 0.5743) * (x2 - 0.5743)))) + (35.0 * ((x3 - 0.8828) * (x3 - 0.8828)));
    double exp1 = exp(-e1);
    double exp2 = exp(-e2);
    double exp3 = exp(-e3);
    double exp4 = exp(-e4);
    return -((((1.0 * exp1) + (1.2 * exp2)) + (3.0 * exp3)) + (3.2 * exp4));
}

int main()
{
    double x1 = 0.5, x2 = 0.7, x3 = 0.9;

    // 调用 ex0 函数
    double result = 0;

    int count = 20000000;

    while (count--)
    {
        /* code */
        result = ex0(x1, x2, x3);
    }

    // 输出结果
    printf("%f", result);

    return 0;
}