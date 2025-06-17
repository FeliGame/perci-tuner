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
    double x = x0, y = y0, z = z0;
    double vx = vx0, vy = vy0, vz = vz0;
    float i = 0.0f;
    while (i < 100.0f)
    {
        float distance = sqrtf((float)(x * x + y * y + z * z));
        float mag = dt / (distance * distance * distance);
        double vxNew = vx - (x * solarMass * mag);
        double x_1 = x + dt * vxNew;

        distance = sqrtf((float)(x * x + y * y + z * z));
        mag = dt / (distance * distance * distance);
        double vyNew = vy - (y * solarMass * mag);
        double y_4 = y + dt * vyNew;

        distance = sqrtf((float)(x * x + y * y + z * z));
        mag = dt / (distance * distance * distance);
        double vzNew = vz - (z * solarMass * mag);
        double z_7 = z + dt * vzNew;

        distance = sqrtf((float)(x * x + y * y + z * z));
        mag = dt / (distance * distance * distance);
        double vx_10 = vx - (x * solarMass * mag);

        distance = sqrtf((float)(x * x + y * y + z * z));
        mag = dt / (distance * distance * distance);
        double vy_13 = vy - (y * solarMass * mag);

        distance = sqrtf((float)(x * x + y * y + z * z));
        mag = dt / (distance * distance * distance);
        double vz_16 = vz - (z * solarMass * mag);

        x = x_1;
        y = y_4;
        z = z_7;
        vx = vx_10;
        vy = vy_13;
        vz = vz_16;

        i += 1.0f;
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
