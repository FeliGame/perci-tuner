// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef double real_t;

static real_t ex0(real_t x0, real_t y0, real_t z0, real_t vx0, real_t vy0, real_t vz0)
{
    real_t dt = (real_t)0.1;
    real_t solarMass = (real_t)39.47841760435743;
    real_t x = x0;
    real_t y = y0;
    real_t z = z0;
    real_t vx = vx0;
    real_t vy = vy0;
    real_t vz = vz0;
    real_t i = (real_t)0.0;
    int tmp = i < (real_t)100.0;
    while (tmp)
    {
        real_t distance = sqrt(((x * x) + (y * y) + (z * z)));
        real_t mag = dt / ((distance * distance) * distance);
        real_t vxNew = vx - ((x * solarMass) * mag);
        real_t x_1 = x + (dt * vxNew);
        real_t distance_2 = sqrt(((x * x) + (y * y) + (z * z)));
        real_t mag_3 = dt / ((distance_2 * distance_2) * distance_2);
        real_t vyNew = vy - ((y * solarMass) * mag_3);
        real_t y_4 = y + (dt * vyNew);
        real_t distance_5 = sqrt(((x * x) + (y * y) + (z * z)));
        real_t mag_6 = dt / ((distance_5 * distance_5) * distance_5);
        real_t vzNew = vz - ((z * solarMass) * mag_6);
        real_t z_7 = z + (dt * vzNew);
        real_t distance_8 = sqrt(((x * x) + (y * y) + (z * z)));
        real_t mag_9 = dt / ((distance_8 * distance_8) * distance_8);
        real_t vx_10 = vx - ((x * solarMass) * mag_9);
        real_t distance_11 = sqrt(((x * x) + (y * y) + (z * z)));
        real_t mag_12 = dt / ((distance_11 * distance_11) * distance_11);
        real_t vy_13 = vy - ((y * solarMass) * mag_12);
        real_t distance_14 = sqrt(((x * x) + (y * y) + (z * z)));
        real_t mag_15 = dt / ((distance_14 * distance_14) * distance_14);
        real_t vz_16 = vz - ((z * solarMass) * mag_15);
        real_t i_17 = i + (real_t)1.0;
        x = x_1;
        y = y_4;
        z = z_7;
        vx = vx_10;
        vy = vy_13;
        vz = vz_16;
        i = i_17;
        tmp = i < (real_t)100.0;
    }
    return x;
}

int main(void)
{
    real_t x0 = (real_t)1.0;
    real_t y0 = (real_t)0.0;
    real_t z0 = (real_t)0.0;
    real_t vx0 = (real_t)0.0;
    real_t vy0 = (real_t)1.0;
    real_t vz0 = (real_t)0.0;
    real_t result = (real_t)0.0;
    int count = 50000;
    while (count--)
    {
        result = ex0(x0, y0, z0, vx0, vy0, vz0);
    }
    printf("%.13e", result);
    return 0;
}
