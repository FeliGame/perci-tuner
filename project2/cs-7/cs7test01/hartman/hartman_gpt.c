#include <math.h>
#include <stdio.h>

double ex0(float x1, float x2, float x3)
{
    float e1 = (3.0f * (x1 - 0.3689f) * (x1 - 0.3689f)) + (10.0f * (x2 - 0.117f) * (x2 - 0.117f)) + (30.0f * (x3 - 0.2673f) * (x3 - 0.2673f));
    float e2 = (0.1f * (x1 - 0.4699f) * (x1 - 0.4699f)) + (10.0f * (x2 - 0.4387f) * (x2 - 0.4387f)) + (35.0f * (x3 - 0.747f) * (x3 - 0.747f));
    float e3 = (3.0f * (x1 - 0.1091f) * (x1 - 0.1091f)) + (10.0f * (x2 - 0.8732f) * (x2 - 0.8732f)) + (30.0f * (x3 - 0.5547f) * (x3 - 0.5547f));
    float e4 = (0.1f * (x1 - 0.03815f) * (x1 - 0.03815f)) + (10.0f * (x2 - 0.5743f) * (x2 - 0.5743f)) + (35.0f * (x3 - 0.8828f) * (x3 - 0.8828f));

    // 使用 double 类型的 exp 保留精度
    double exp1 = exp(-e1);
    double exp2 = exp(-e2);
    double exp3 = exp(-e3);
    double exp4 = exp(-e4);

    return -(1.0 * exp1 + 1.2 * exp2 + 3.0 * exp3 + 3.2 * exp4);
}

int main()
{
    float x1 = 0.5f;
    float x2 = 0.7f;
    float x3 = 0.9f;
    double result = 0;
    int count = 5000000;
    while (count--)
    {
        result = ex0(x1, x2, x3);
    }
    printf("%.13e\n", result);
    return 0;
}
