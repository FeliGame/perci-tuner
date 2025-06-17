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
    float s_dLon = sinf((float)dLon); // Use float and sinf for numerator
    double c_dLon = cos(dLon);
    float numerator = (float)c_lat2 * s_dLon; // Compute numerator in float
    double denominator = c_lat1 * s_lat2 - s_lat1 * c_lat2 * c_dLon; // Denominator in double
    return atan(denominator == 0.0 ? 0.0 : (double)numerator / denominator); // Cast numerator to double
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