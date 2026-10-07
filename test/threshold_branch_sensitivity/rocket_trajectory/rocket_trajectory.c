// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

#include <math.h>
#include <stdio.h>

double ex0(double Mf, double A)
{
    double R = 6400000.0;
    double G = 6.67428e-11;
    double Mt = 5.9736e+24;
    double dt = 0.1;

    double r0 = R + 400000.0;
    double viss = sqrt(G * Mt / r0);
    double vteta0 = viss / r0;

    double rf = R;
    double vlrad = sqrt(G * Mt / R) / r0;
    double vtetaf = 1.1 * vlrad;

    double u1 = r0;
    double u2 = 0.0;
    double u3 = 0.0;
    double u4 = vteta0;
    double w1 = rf;
    double w2 = 0.0;
    double w4 = vtetaf;

    double t = 0.0;
    double mf = Mf;
    double x = 0.0;

    for (int i = 0; i < 2000000; ++i)
    {
        t += dt;
        mf -= A * dt;

        double u1_new = u1 + u2 * dt;
        double u3_new = u3 + u4 * dt;
        double u2_new = u2 + ((-G * Mt / (u1 * u1)) + u1 * u4 * u4) * dt;
        double u4_new = u4 + (-2.0 * u2 * u4 / u1) * dt;

        double w1_new = w1 + w2 * dt;
        double w3_new = w4 * dt;  // no need to store this unless used
        double thrust_r = (mf > 0.0) ? (A * w2 / mf) : 0.0;
        double w2_new = w2 + ((-G * Mt / (w1 * w1)) + w1 * w4 * w4 + thrust_r) * dt;

        double thrust_t = (mf > 0.0) ? (A * w4 / mf) : 0.0;
        double w4_new = w4 + ((-2.0 * w2 * w4 / w1) + thrust_t) * dt;

        x = u1_new * cos(u3_new);

        u1 = u1_new;
        u2 = u2_new;
        u3 = u3_new;
        u4 = u4_new;
        w1 = w1_new;
        w2 = w2_new;
        w4 = w4_new;

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

    for (int i = 0; i < 1000; ++i)
        result = ex0(Mf, A);

    printf("%.13e\n", result);
    return 0;
}
