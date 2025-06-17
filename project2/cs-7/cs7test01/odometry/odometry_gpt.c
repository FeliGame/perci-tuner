#include <math.h>
#include <stdio.h>

double ex0(double sr_42_, double sl_42_)
{
    float inv_l = 0.1f;
    float c = 12.34f;
    float delta_dl = 0.0f;
    float delta_dr = 0.0f;
    double delta_d = 0.0;
    double delta_theta = 0.0;
    double arg = 0.0;
    double cosi = 0.0;
    double sini = 0.0;
    double x = 0.0;
    double y = 0.0;
    double theta = -0.985;
    float t = 0.0f;
    float sl = (float)sl_42_;
    float sr = (float)sr_42_;
    float j = 0.0f;

    while (t < 1000.0f)
    {
        delta_dl = c * sl;
        delta_dr = c * sr;

        delta_d = 0.5 * (double)(delta_dl + delta_dr);
        delta_theta = (double)(delta_dr - delta_dl) * inv_l;

        arg = theta + 0.5 * delta_theta;

        // 近似 cos(arg)
        double arg2 = arg * arg;
        double arg4 = arg2 * arg2;
        cosi = 1.0 - 0.5 * arg2 + 0.0416666666 * arg4;

        // 近似 sin(arg)
        double arg3 = arg2 * arg;
        double arg5 = arg3 * arg2;
        sini = arg - 0.1666666666 * arg3 + 0.008333333 * arg5;

        x += delta_d * cosi;
        y += delta_d * sini;

        theta += delta_theta;
        t += 1.0f;

        sl = (j == 50.0f) ? sr : sl;
        sr = (j == 50.0f) ? sl : sr;
        j = (j == 50.0f) ? 0.0f : j + 1.0f;
    }

    return x;
}

int main()
{
    double sr_42 = 1.0;
    double sl_42 = 0.8;
    double result = 0;
    int count = 10000;
    while (count--)
    {
        result = ex0(sr_42, sl_42);
    }
    printf("%.13e\n", result);
    return 0;
}
