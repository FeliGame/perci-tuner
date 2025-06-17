#include <math.h>
#include <stdio.h>

double ex0(double y, double yd)
{
    float eps = 0.01f;
    float Dc = -1280.0f;
    float Ac00 = 0.499f;
    float Ac01 = -0.05f;
    float Ac10 = 0.01f;
    float Ac11 = 1.0f;
    float Bc0 = 1.0f;
    float Bc1 = 0.0f;
    float Cc0 = 564.48f;
    float Cc1 = 0.0f;
    double yc = 0.0;
    float xc0 = 0.0f;
    double xc1 = 0.0;
    double e = 1.0;
    int tmp = e > (double)eps;
    while (tmp)
    {
        double v = y - yd;
        float tmp_1;
        if (v < -1.0)
        {
            tmp_1 = -1.0f;
        }
        else if (1.0 < v)
        {
            tmp_1 = 1.0f;
        }
        else
        {
            tmp_1 = (float)v;
        }
        yc = (double)tmp_1;
        xc0 = (Ac00 * xc0) + ((Ac01 * (float)xc1) + (Bc0 * (float)yc));
        xc1 = ((double)Ac10 * (double)xc0) + ((double)Ac11 * xc1) + ((double)Bc1 * yc);
        e = fabsf((float)(yc - xc1));
        tmp = e > (double)eps;
    }
    return xc1;
}

int main()
{
    double y = 50.0;
    double yd = 60.0;
    double result = 0;
    int count = 500000;
    while (count--)
    {
        result = ex0(y, yd);
    }
    printf("%.13e\n", result);
    return 0;
}