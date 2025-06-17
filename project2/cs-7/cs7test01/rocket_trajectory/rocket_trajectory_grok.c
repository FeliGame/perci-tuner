#include <math.h>
#include <stdio.h>

double ex0(double Mf, double A)
{
    double R = 6400000.0;
    double G = 6.67428e-11;
    double Mt = 5.9736e+24;
    float dt = 0.1f;
    double r0 = (400.0 * 1000.0) + R;
    double teta0 = 0.0;
    double viss = sqrt((G * Mt) / r0);
    double vteta0 = viss / r0;

    double rf = R;
    double vlrad = sqrt((G * Mt) / R) / r0;
    double vtetaf = 1.1 * vlrad;

    double t_i = 0.0;
    double mf_i = Mf;
    
    double u1_i;
    double u2_i;
    double u3_i;
    double u4_i;

    float w1_i;
    float w2_i;
    float w3_i;
    float w4_i;

    double u1 = r0;
    double u2 = 0.0;
    double u3 = teta0;
    double u4 = vteta0;

    float w1 = (float)rf;
    float w2 = 0.0f;
    float w3 = 0.0f;
    float w4 = (float)vtetaf;

    double t = 0.0;
    double mf = Mf;
    double x = 0.0;

    for (int i = 0; i < 2000000; i++)
    {
        t_i = t + (double)dt;
        mf_i = mf - A * (double)dt;

        u1_i = u1 + u2 * (double)dt;
        u3_i = u3 + u4 * (double)dt;
        u2_i = u2 + ((-G * Mt / (u1 * u1)) + u1 * u4 * u4) * (double)dt;
        u4_i = u4 + (-2.0 * u2 * u4 / u1) * (double)dt;

        w1_i = w1 + w2 * dt;
        w3_i = w3 + w4 * dt;

        float thrust_a_r = (mf > 0.0) ? ((float)A * w2 / (float)mf) : 0.0f;
        w2_i = w2 + ((-G * Mt / (w1 * w1)) + w1 * w4 * w4 + thrust_a_r) * dt;

        float thrust_a_t = (mf > 0.0) ? ((float)A * w4 / (float)mf) : 0.0f;
        w4_i = w4 + ((-2.0f * w2 * w4 / w1) + thrust_a_t) * dt;

        x = u1_i * cos(u3_i);

        u1 = u1_i;
        u2 = u2_i;
        u3 = u3_i;
        u4 = u4_i;
        w1 = w1_i;
        w2 = w2_i;
        w3 = w3_i;
        w4 = w4_i;
        t = t_i;
        mf = mf_i;

        if (mf <= 0.0)
            break;
    }

    return x;
}

int main()
{
    double Mf = 5000;
    double A = 10;
    double result = 0;
    int count = 1000;
    while (count--)
    {
        result = ex0(Mf, A);
    }
    printf("%.13e\n", result);
    return 0;
}