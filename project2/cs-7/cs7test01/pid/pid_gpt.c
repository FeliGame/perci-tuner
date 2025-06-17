#include <stdio.h>
#include <math.h>

double ex0(double m, double kp, double ki, double kd, double c)
{
    const float dt = 0.5f;
    const float invdt = 1.0f / dt;
    double e = 0.0, r = 0.0, m_1 = m;
    float p = 0.0f, i = 0.0f, d = 0.0f;
    float eold = 0.0f, t = 0.0f;

    while (t < 100.0f)
    {
        e = c - m_1;
        p = (float)(kp * e);
        i += (float)(ki * dt * e);
        d = (float)(kd * invdt * (e - eold));
        r = (double)(p + i + d);
        m_1 += 0.01 * r;
        eold = (float)e;
        t += dt;
    }
    return m_1;
}

int main()
{
    double m = 0.0, kp = 1.0, ki = 0.1, kd = 0.5, c = 10.0;
    double result = 0;
    int count = 100000;
    while (count--)
    {
        result = ex0(m, kp, ki, kd, c);
    }
    printf("%.13e\n", result);
    return 0;
}
