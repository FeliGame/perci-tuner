#include <math.h>
#include <stdio.h>

double ex0(double Mf, double A)
{
    float R = 6400000.0;
    float G = 6.67428e-11;
    float Mt = 5.9736e+24;
    float dt = 0.1;
    float r0 = (400.0 * 1000.0) + R;
    double teta0 = 0.0;
    float viss = sqrt((G * Mt) / r0);
    float vteta0 = viss / r0;

    float rf = R;
    float vlrad = sqrt((G * Mt) / R) / r0;
    float vtetaf = 1.1 * vlrad;

    float t_i = 0.0;
    double mf_i = Mf;
    
    double u1_i;
    double u2_i;
    double u3_i;
    double u4_i;

    double w1_i;
    double w2_i;
    double w3_i;
    double w4_i;

    double u1 = r0;
    double u2 = 0.0;
    double u3 = teta0;
    double u4 = vteta0;

    double w1 = rf;
    double w2 = 0.0;
    double w3 = 0.0;
    double w4 = vtetaf;

    double t = 0.0;
    double mf = Mf;
    double x = 0.0;

    for (int i = 0; i < 2000000; i++)
    {
        t_i = t + dt;
        mf_i = mf - A * dt;

        u1_i = u1 + u2 * dt;
        u3_i = u3 + u4 * dt;
        u2_i = u2 + ((-G * Mt / (u1 * u1)) + u1 * u4 * u4) * dt;
        u4_i = u4 + (-2.0 * u2 * u4 / u1) * dt;

        w1_i = w1 + w2 * dt;
        w3_i = w3 + w4 * dt;

        float thrust_a_r = (mf > 0.0) ? (A * (float)w2 / (float)mf) : 0.0f;
        w2_i = w2 + ((-G * Mt / (w1 * w1)) + w1 * w4 * w4 + thrust_a_r) * dt;

        float thrust_a_t = (mf > 0.0) ? (A * (float)w4 / (float)mf) : 0.0;
        w4_i = w4 + ((-2.0 * w2 * w4 / w1) + thrust_a_t) * dt;

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