#include <math.h>
#include <stdio.h>

double ex0(double t0, double w0, double N)
{
    float h = 0.01f; // Changed to float
    float L = 2.0f;  // Changed to float
    float g = 9.80665f; // Changed to float
    double t = t0;
    double w = w0;
    float n = 0.0f;  // Changed to float
    while (n < (float)N) // Cast N to float for comparison
    {
        float k1w = (-g / L) * sinf((float)t); // Use float and sinf
        float k2t = (float)w + (h / 2.0f) * k1w; // Cast w to float
        double t_1 = t + (double)h * (double)k2t; // Cast to double for update
        float k2w = (-g / L) * sinf((float)(t + (double)h / 2.0 * w)); // Use float and sinf
        double w_2 = w + (double)h * (double)k2w; // Cast to double for update
        t = t_1;
        w = w_2;
        n += 1.0f;
    }
    return t;
}

int main()
{
    double t0 = 0.0;
    double w0 = 1.0;
    double N = 1000;
    double result = 0;
    int count = 5000;
    while (count--)
    {
        result = ex0(t0, w0, N);
    }
    printf("%.13e\n", result);
    return 0;
}