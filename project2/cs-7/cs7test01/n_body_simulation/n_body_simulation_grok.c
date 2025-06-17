#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <inttypes.h>
#include <time.h>
#include <stdbool.h>

#define TRUE 1
#define FALSE 0

double ex0(double x0, double y0, double z0, double vx0, double vy0, double vz0)
{
    float dt = 0.1f;
    float solarMass = 39.47841760435743f;
    double x = x0;
    double y = y0;
    double z = z0;
    double vx = vx0;
    double vy = vy0;
    double vz = vz0;
    float i = 0.0f;
    int tmp = i < 100.0f;
    while (tmp)
    {
        float distance = sqrtf((float)(((x * x) + (y * y)) + (z * z)));
        float mag = dt / ((distance * distance) * distance);
        float vxNew = (float)(vx - ((x * (double)solarMass) * (double)mag));
        double x_1 = x + ((double)dt * (double)vxNew);
        float distance_2 = sqrtf((float)(((x * x) + (y * y)) + (z * z)));
        float mag_3 = dt / ((distance_2 * distance_2) * distance_2);
        float vyNew = (float)(vy - ((y * (double)solarMass) * (double)mag_3));
        double y_4 = y + ((double)dt * (double)vyNew);
        float distance_5 = sqrtf((float)(((x * x) + (y * y)) + (z * z)));
        float mag_6 = dt / ((distance_5 * distance_5) * distance_5);
        float vzNew = (float)(vz - ((z * (double)solarMass) * (double)mag_6));
        double z_7 = z + ((double)dt * (double)vzNew);
        float distance_8 = sqrtf((float)(((x * x) + (y * y)) + (z * z)));
        float mag_9 = dt / ((distance_8 * distance_8) * distance_8);
        float vx_10 = (float)(vx - ((x * (double)solarMass) * (double)mag_9));
        float distance_11 = sqrtf((float)(((x * x) + (y * y)) + (z * z)));
        float mag_12 = dt / ((distance_11 * distance_11) * distance_11);
        float vy_13 = (float)(vy - ((y * (double)solarMass) * (double)mag_12));
        float distance_14 = sqrtf((float)(((x * x) + (y * y)) + (z * z)));
        float mag_15 = dt / ((distance_14 * distance_14) * distance_14);
        float vz_16 = (float)(vz - ((z * (double)solarMass) * (double)mag_15));
        float i_17 = i + 1.0f;
        x = x_1;
        y = y_4;
        z = z_7;
        vx = (double)vx_10;
        vy = (double)vy_13;
        vz = (double)vz_16;
        i = i_17;
        tmp = i < 100.0f;
    }
    return x;
}

int main()
{
    double x0 = 1.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double vx0 = 0.0;
    double vy0 = 1.0;
    double vz0 = 0.0;
    double result = 0;
    int count = 50000;
    while (count--)
    {
        result = ex0(x0, y0, z0, vx0, vy0, vz0);
    }
    printf("%.13e", result);
    return 0;
}