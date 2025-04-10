#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Define PI for FFT computation
#define PI 3.14159265358979323846

// Function to compute the FFT recursively
void fft_recursive(double *real, double *imag, int n)
{
    if (n <= 1)
        return;

    // Allocate memory for even and odd parts
    double *real_even = (double *)malloc(n / 2 * sizeof(double));
    double *imag_even = (double *)malloc(n / 2 * sizeof(double));
    double *real_odd = (double *)malloc(n / 2 * sizeof(double));
    double *imag_odd = (double *)malloc(n / 2 * sizeof(double));

    // Divide into even and odd indices
    for (int i = 0; i < n / 2; i++)
    {
        real_even[i] = real[i * 2];
        imag_even[i] = imag[i * 2];
        real_odd[i] = real[i * 2 + 1];
        imag_odd[i] = imag[i * 2 + 1];
    }

    // Recursive calls
    fft_recursive(real_even, imag_even, n / 2);
    fft_recursive(real_odd, imag_odd, n / 2);

    // Combine
    for (int k = 0; k < n / 2; k++)
    {
        double t_real = cos(-2 * PI * k / n) * real_odd[k] - sin(-2 * PI * k / n) * imag_odd[k];
        double t_imag = sin(-2 * PI * k / n) * real_odd[k] + cos(-2 * PI * k / n) * imag_odd[k];

        real[k] = real_even[k] + t_real;
        imag[k] = imag_even[k] + t_imag;
        real[k + n / 2] = real_even[k] - t_real;
        imag[k + n / 2] = imag_even[k] - t_imag;
    }

    // Free allocated memory
    free(real_even);
    free(imag_even);
    free(real_odd);
    free(imag_odd);
}

// Main function to test FFT
int main()
{
    // Define the size of the FFT (must be a power of 2)
    int n = 12;

    // Allocate memory for real and imaginary parts
    double *real = (double *)malloc(n * sizeof(double));
    double *imag = (double *)malloc(n * sizeof(double));

    // Initialize input signal (example: a simple cosine wave)
    for (int i = 0; i < n; i++)
    {
        real[i] = cos(2 * PI * i / n); // Example signal
        imag[i] = 0.0;                 // Imaginary part initialized to 0
    }

    // Perform FFT
    fft_recursive(real, imag, n);

    // Compute the power spectrum and sum of all frequencies
    double power_sum = 0.0;
    for (int i = 0; i < n; i++)
    {
        double magnitude = sqrt(real[i] * real[i] + imag[i] * imag[i]);
        power_sum += magnitude * magnitude;
    }

    // Output the result
    printf("Sum of power spectrum: %f\n", power_sum);

    // Free allocated memory
    free(real);
    free(imag);

    return 0;
}
