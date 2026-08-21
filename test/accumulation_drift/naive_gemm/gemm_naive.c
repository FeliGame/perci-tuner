// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/*
 * gemm_longdouble_target_only.c
 *
 * Minimal long double baseline Naive GEMM program for mixed-precision tools.
 *
 * Purpose:
 *   This version is intended to be used as the direct input program for a
 *   variable/function-level mixed-precision tuning tool.
 *
 *   It intentionally DOES NOT include:
 *     - gemm_reference_long_double()
 *     - compute_error_metrics()
 *     - high-precision reference checking
 *
 *   The program only provides:
 *     - data generation
 *     - gemm_target()
 *     - timing
 *     - checksum output
 *
 * Build:
 *   gcc   -std=c99 -O3 -fno-fast-math -ffp-contract=off gemm_longdouble_target_only.c -lm -o gemm_ld_target
 *   clang -std=c99 -O3 -fno-fast-math -ffp-contract=off gemm_longdouble_target_only.c -lm -o gemm_ld_target
 *
 * Run:
 *   ./gemm_ld_target [M] [N] [K] [iters] [seed] [data_mode]
 *
 * Examples:
 *   ./gemm_ld_target
 *   ./gemm_ld_target 128 128 128 3 1 0
 *   ./gemm_ld_target 128 128 512 3 1 2
 *
 * Data modes:
 *   0: uniform random in [-1, 1]
 *   1: uniform random in [0, 1]
 *   2: alternating-sign cancellation pattern
 *   3: wide dynamic range random values
 *   4: deterministic smooth pattern
 *
 * Notes:
 *   long double is platform-dependent:
 *     - x86-64: often x87 80-bit extended precision
 *     - AArch64: often IEEE binary128 / software fp128
 *     - some platforms: same as double
 */

#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef RESTRICT
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    define RESTRICT restrict
#  else
#    define RESTRICT
#  endif
#endif

static double wall_time_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
    }
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static double rng_uniform01_ld(uint64_t *state) {
    return (double)(splitmix64(state) >> 11) * (1.0 / 9007199254740992.0);
}

static double rng_uniform_pm1_ld(uint64_t *state) {
    return 2.0 * rng_uniform01_ld(state) - 1.0;
}

static int checked_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static void *xmalloc_array(size_t count, size_t elem_size, const char *name) {
    size_t bytes = 0;
    if (!checked_mul_size(count, elem_size, &bytes)) {
        fprintf(stderr, "Allocation size overflow for %s\n", name);
        exit(EXIT_FAILURE);
    }

    void *ptr = malloc(bytes);
    if (!ptr) {
        fprintf(stderr, "malloc failed for %s: %zu bytes\n", name, bytes);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

static void fill_matrix(double *X, int rows, int cols, int data_mode, uint64_t seed, int is_B) {
    uint64_t rng = seed + (is_B ? UINT64_C(0x123456789abcdef0) : UINT64_C(0xfedcba9876543210));

    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            const size_t idx = (size_t)i * (size_t)cols + (size_t)j;
            double v = 0.0;

            switch (data_mode) {
                case 0:
                    v = rng_uniform_pm1_ld(&rng);
                    break;

                case 1:
                    v = rng_uniform01_ld(&rng);
                    break;

                case 2: {
                    const double sign = ((i + j + (is_B ? 1 : 0)) & 1) ? -1.0 : 1.0;
                    const double perturb = 1.0e-3 * rng_uniform_pm1_ld(&rng);
                    v = sign * (1.0 + perturb);
                    break;
                }

                case 3: {
                    const double sign = (rng_uniform01_ld(&rng) < 0.5) ? -1.0 : 1.0;
                    const double exponent = -6.0 + 12.0 * rng_uniform01_ld(&rng);
                    v = sign * pow(10.0, exponent);
                    break;
                }

                case 4:
                    v = sin(0.013 * (double)(i + 1)
                           + 0.017 * (double)(j + 1)
                           + 0.001 * (double)(seed % 1000))
                      * cos(0.019 * (double)(i + 1)
                           - 0.011 * (double)(j + 1)
                           + (is_B ? 0.25 : 0.0));
                    break;

                default:
                    fprintf(stderr, "Unsupported data_mode=%d\n", data_mode);
                    exit(EXIT_FAILURE);
            }

            X[idx] = v;
        }
    }
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET
 * ============================================================
 *
 * This is the only core computation.
 * Your mixed-precision tool can search variables/functions in this function.
 */
void gemm_target(int M, int N, int K,
                 const double * RESTRICT A,
                 const double * RESTRICT B,
                 double * RESTRICT C) {
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            double sum = 0.0;

            for (int k = 0; k < K; ++k) {
                double a = A[(size_t)i * (size_t)K + (size_t)k];
                double b = B[(size_t)k * (size_t)N + (size_t)j];
                double prod = a * b;
                sum += prod;
            }

            C[(size_t)i * (size_t)N + (size_t)j] = sum;
        }
    }
}

static double checksum_matrix(const double *C, size_t count) {
    double checksum = 0.0;

    for (size_t i = 0; i < count; ++i) {
        /*
         * Weighted checksum reduces the chance of simple cancellation hiding
         * large output changes.
         */
        const double weight = 1.0 + (double)(i % 97) * 1.0e-4;
        checksum += C[i] * weight;
    }

    return checksum;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [M] [N] [K] [iters] [seed] [data_mode]\n"
            "\n"
            "Defaults: M=128 N=128 K=128 iters=3 seed=1 data_mode=0\n"
            "\n"
            "data_mode:\n"
            "  0: uniform random in [-1, 1]\n"
            "  1: uniform random in [0, 1]\n"
            "  2: alternating-sign cancellation pattern\n"
            "  3: wide dynamic range random values\n"
            "  4: deterministic smooth pattern\n",
            prog);
}

static int parse_int_arg(const char *s, const char *name, int min_value) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0' || v < min_value || v > INT32_MAX) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    return (int)v;
}

int main(int argc, char **argv) {
    int M = 128;
    int N = 128;
    int K = 128;
    int iters = 3;
    int seed = 1;
    int data_mode = 0;

    if (argc > 7) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) M = parse_int_arg(argv[1], "M", 1);
    if (argc >= 3) N = parse_int_arg(argv[2], "N", 1);
    if (argc >= 4) K = parse_int_arg(argv[3], "K", 1);
    if (argc >= 5) iters = parse_int_arg(argv[4], "iters", 1);
    if (argc >= 6) seed = parse_int_arg(argv[5], "seed", 0);
    if (argc >= 7) data_mode = parse_int_arg(argv[6], "data_mode", 0);

    if (data_mode < 0 || data_mode > 4) {
        fprintf(stderr, "data_mode must be in [0, 4]\n");
        return EXIT_FAILURE;
    }

    size_t size_A = 0, size_B = 0, size_C = 0;
    if (!checked_mul_size((size_t)M, (size_t)K, &size_A) ||
        !checked_mul_size((size_t)K, (size_t)N, &size_B) ||
        !checked_mul_size((size_t)M, (size_t)N, &size_C)) {
        fprintf(stderr, "Matrix dimension overflow\n");
        return EXIT_FAILURE;
    }

    double *A = (double *)xmalloc_array(size_A, sizeof(double), "A");
    double *B = (double *)xmalloc_array(size_B, sizeof(double), "B");
    double *C = (double *)xmalloc_array(size_C, sizeof(double), "C");

    fill_matrix(A, M, K, data_mode, (uint64_t)seed, 0);
    fill_matrix(B, K, N, data_mode, (uint64_t)seed, 1);

    double total_kernel_time = 0.0;

    for (int it = 0; it < iters; ++it) {
        const double t0 = wall_time_seconds();
        gemm_target(M, N, K, A, B, C);
        const double t1 = wall_time_seconds();
        total_kernel_time += (t1 - t0);
    }

    const double avg_kernel_time = total_kernel_time / (double)iters;
    const double flops = 2.0 * (double)M * (double)N * (double)K;
    const double gflops = flops / avg_kernel_time / 1.0e9;
    const double checksum = checksum_matrix(C, size_C);

    printf("benchmark = naive_gemm_long_double_target_only\n");
    printf("M = %d\n", M);
    printf("N = %d\n", N);
    printf("K = %d\n", K);
    printf("iters = %d\n", iters);
    printf("seed = %d\n", seed);
    printf("data_mode = %d\n", data_mode);
    printf("target = long_double_gemm_target_only\n");
    printf("sizeof_long_double = %zu\n", sizeof(double));
    printf("LDBL_MANT_DIG = %d\n", DBL_MANT_DIG);
    printf("LDBL_DIG = %d\n", DBL_DIG);
    printf("avg_kernel_time_sec = %.9f\n", avg_kernel_time);
    printf("gflops = %.9f\n", gflops);
    printf("checksum = %.21e\n", checksum);

    free(A);
    free(B);
    free(C);

    return EXIT_SUCCESS;
}
