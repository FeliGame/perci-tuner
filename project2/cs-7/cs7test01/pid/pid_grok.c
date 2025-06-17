#include <stdio.h>
#include <math.h>

double ex0(double m, double kp, double ki, double kd, double c)
{
    float dt = 0.5f;
    float invdt = 1.0f / dt;
    double e = 0.0;
    float p = 0.0f;
    double i = 0.0;
    float d = 0.0f;
    double r = 0.0;
    double m_1 = m;
    float eold = 0.0f;
    float t = 0.0f;
    while (t < 100.0f)
    {
        e = c - m_1;
        p = (float)kp * (float)e;
        i += (double)((float)ki * dt * (float)e);
        d = (float)kd * invdt * ((float)e - eold);
        r = (double)p + i + (double)d;
        m_1 += (double)(0.01f * (float)r);
        eold = (float)e;
        t += dt;
    }
    return m_1;
}

int main()
{
    double m = 0.0;
    double kp = 1.0;
    double ki = 0.1;
    double kd = 0.5;
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