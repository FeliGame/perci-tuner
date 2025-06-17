#include <math.h>
#include <stdio.h>

double ex0(double Mf, double A)
{
    double R = 6400000.0;
    double G = 6.67428e-11;
    double Mt = 5.9736e+24;
    double dt = 0.1;
    double r0 = (400.0 * 1000.0) + R;
    double teta0 = 0.0;
    double viss = sqrt((G * Mt) / r0);
    double vteta0 = viss / r0;

    double rf = R;
    double vlrad = sqrt((G * Mt) / R) / r0;
    double vtetaf = 1.1 * vlrad;

    double t_i = 0.0;
    double mf_i = Mf;
    double u1_i, u2_i, u3_i, u4_i;
    double w1_i, w2_i, w3_i, w4_i;

    double u1 = r0, u2 = 0.0, u3 = teta0, u4 = vteta0;
    double w1 = rf, w2 = 0.0, w3 = 0.0, w4 = vtetaf;
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

        double thrust_a_r = (mf > 0.0) ? (A * w2 / mf) : 0.0;
        w2_i = w2 + ((-G * Mt / (w1 * w1)) + w1 * w4 * w4 + thrust_a_r) * dt;

        double thrust_a_t = (mf > 0.0) ? (A * w4 / mf) : 0.0;
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
    double result = ex0(Mf, A);
    printf("%.13e\n", result);
    return 0;
}