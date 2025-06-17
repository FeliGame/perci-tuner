#include <math.h>
#include <stdio.h>

double ex0(double t, double resistance, double frequency, double inductance, double maxVoltage)
{
    float pi = 3.14159265359;
    double impedance_re = resistance;
    float impedance_im = (2.0 * pi) * (double)frequency * (double)inductance;
    float denom = (impedance_re * impedance_re) + (impedance_im * impedance_im);
    double current_re = (maxVoltage * impedance_re) / denom;
    double current_im = -(maxVoltage * impedance_im) / denom;
    double maxCurrent = sqrt((current_re * current_re) + (current_im * current_im));
    float theta = atanf((float)current_im / current_re);
    return maxCurrent * cos((2.0 * (double)pi * frequency * t) + (double)theta);
}

int main()
{
    double t = 0.01;
    double resistance = 10.0;
    double frequency = 50.0;
    double inductance = 0.1;
    double maxVoltage = 220.0;

    double current = 0;
    int count = 1000000;
    while (count--)
    {
        current = ex0(t, resistance, frequency, inductance, maxVoltage);
    }
    printf("%.13e\n", current);

    return 0;
}