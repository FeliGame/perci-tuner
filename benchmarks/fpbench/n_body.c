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
    double dt = 0.1;
    double solarMass = 39.47841760435743;
    double x = x0;
    double y = y0;
    double z = z0;
    double vx = vx0;
    double vy = vy0;
    double vz = vz0;
    double i = 0.0;
    int tmp = i < 100.0;
    while (tmp)
    {
        double distance = sqrt((((x * x) + (y * y)) + (z * z)));
        double mag = dt / ((distance * distance) * distance);
        double vxNew = vx - ((x * solarMass) * mag);
        double x_1 = x + (dt * vxNew);
        double distance_2 = sqrt((((x * x) + (y * y)) + (z * z)));
        double mag_3 = dt / ((distance_2 * distance_2) * distance_2);
        double vyNew = vy - ((y * solarMass) * mag_3);
        double y_4 = y + (dt * vyNew);
        double distance_5 = sqrt((((x * x) + (y * y)) + (z * z)));
        double mag_6 = dt / ((distance_5 * distance_5) * distance_5);
        double vzNew = vz - ((z * solarMass) * mag_6);
        double z_7 = z + (dt * vzNew);
        double distance_8 = sqrt((((x * x) + (y * y)) + (z * z)));
        double mag_9 = dt / ((distance_8 * distance_8) * distance_8);
        double vx_10 = vx - ((x * solarMass) * mag_9);
        double distance_11 = sqrt((((x * x) + (y * y)) + (z * z)));
        double mag_12 = dt / ((distance_11 * distance_11) * distance_11);
        double vy_13 = vy - ((y * solarMass) * mag_12);
        double distance_14 = sqrt((((x * x) + (y * y)) + (z * z)));
        double mag_15 = dt / ((distance_14 * distance_14) * distance_14);
        double vz_16 = vz - ((z * solarMass) * mag_15);
        double i_17 = i + 1.0;
        x = x_1;
        y = y_4;
        z = z_7;
        vx = vx_10;
        vy = vy_13;
        vz = vz_16;
        i = i_17;
        tmp = i < 100.0;
    }
    return x;
}

int main()
{
    double x0 = 1.0, y0 = 0.0, z0 = 0.0;    // 初始位置 (1, 0, 0)
    double vx0 = 0.0, vy0 = 1.0, vz0 = 0.0; // 初始速度 (0, 1, 0)

    // 调用 ex0 函数并输出结果
    int count = 500000;
    double result = 0;
    while (count--)
    {
        /* code */
        result = ex0(x0, y0, z0, vx0, vy0, vz0);
    }
    printf("%.13f", result);
    return 0;
}
