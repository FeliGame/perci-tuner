// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

#include <math.h>
#include <stdio.h>

#define M_PI 3.14159265358979323846

double ex0(double lat1, double lat2, double lon1, double lon2)
{
    double dLon = lon2 - lon1;
    double s_lat1 = sin(lat1);
    double c_lat1 = cos(lat1);
    double s_lat2 = sin(lat2);
    double c_lat2 = cos(lat2);
    double s_dLon = sin(dLon);
    double c_dLon = cos(dLon);
    return atan((c_lat2 * s_dLon) / ((c_lat1 * s_lat2) - (s_lat1 * c_lat2 * c_dLon)));
}

int main(int argc, char **argv)
{
    double lat1 = 40.7128 * M_PI / 180.0;
    double lon1 = -74.0060 * M_PI / 180.0;
    double lat2 = 34.0522 * M_PI / 180.0;
    double lon2 = -118.2437 * M_PI / 180.0;
    double result = 0.0;
    int count = 1000000;
    while (count--)
    {
        result = ex0(lat1, lat2, lon1, lon2);
    }
    printf("Calculated angle (in radians): %.13e\n", result);
    return 0;
}