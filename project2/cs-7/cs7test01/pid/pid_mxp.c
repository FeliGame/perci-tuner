#include <stdio.h>
#include <math.h>

double ex0(double m, double kp, double ki, double kd, double c)
{
    double dt = 0.5;
    double invdt = 1.0 / dt;
    double e = 0.0;
    double p = 0.0;
    double i = 0.0;
    double d = 0.0;
    double r = 0.0;
    double m_1 = m;
    double eold = 0.0;
    double t = 0.0;
    while (t < 100.0)
    {
        e = c - m_1;
        p = kp * e;
        i += ki * dt * e;
        d = kd * invdt * (e - eold);
        r = p + i + d;
        m_1 += 0.01 * r;
        eold = e;
        t += dt;
    }
    return m_1;
}

int main()
{
    double m = 0.0;
    double kp = 1.0;
    float ki = 0.1;
    float kd = 0.5;
    double c = 10.0;
    double result = 0;
    int count = 100000;
    while (count--)
    {
        result = ex0(m, kp, ki, kd, c);
    }
    printf("%.13e\n", result);
    return 0;
}