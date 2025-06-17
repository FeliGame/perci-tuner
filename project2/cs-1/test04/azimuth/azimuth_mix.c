#include <math.h>
#include <stdio.h>

#define M_PI 3.14159265358979323846

double ex0(double lat1, double lat2, double lon1, double lon2)
{
    float dLon = lon2 - lon1;
    float s_lat1 = sin(lat1);
    float c_lat1 = cos(lat1);
    float s_lat2 = sin(lat2);
    float c_lat2 = cos(lat2);
    float s_dLon = sin(dLon);
    float c_dLon = cos(dLon);
    return atan((c_lat2 * s_dLon) / ((c_lat1 * s_lat2) - (s_lat1 * c_lat2 * c_dLon)));
}

int main(int argc, char **argv)
{
    float lat1 = 40.7128 * M_PI / 180.0;
    float lon1 = -74.0060 * M_PI / 180.0;
    float lat2 = 34.0522 * M_PI / 180.0;
    float lon2 = -118.2437 * M_PI / 180.0;
    float result = 0.0;
    int count = 1000000;
    while (count--)
    {
        result = ex0(lat1, lat2, lon1, lon2);
    }
    printf("Calculated angle (in radians): %.13e\n", result);
    return 0;
}