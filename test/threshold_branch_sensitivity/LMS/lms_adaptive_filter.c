// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -arg=2000 -arg=8 -arg=1 -arg=0 -arg=0 -arg=0.01 -arg=1 -include-func=lms_predict,lms_update -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/*
 * lms_adaptive_filter_target.c
 *
 * LMS adaptive FIR filter benchmark for mixed-precision tuning tools.
 *
 * Mathematical structure:
 *
 *   y[n] = sum_{i=0}^{N-1} w[i] * x_vec[i]
 *   e[n] = d[n] - y[n]
 *   w[i] = w[i] + 2 * mu * e[n] * x_vec[i]
 *
 * Typical applications:
 *   - echo cancellation
 *   - noise cancellation
 *   - adaptive system identification
 *
 * Purpose:
 *   This program exposes classic DSP mixed-precision issues:
 *     - adaptive recursive weight updates
 *     - step-size mu sensitivity
 *     - dot-product accumulation
 *     - long-time convergence sensitivity
 *     - cancellation in error e = d - y
 *
 * Intended precision-search targets:
 *   - lms_predict()
 *   - lms_update()
 *   - lms_process()
 *
 * This file intentionally does not include a high-precision reference
 * implementation. Use an external harness to compare the original baseline
 * and the mixed-precision transformed version.
 *
 * Build:
 *   gcc   -std=c99 -O3 -fno-fast-math -ffp-contract=off lms_adaptive_filter_target.c -lm -o lms_filter
 *   clang -std=c99 -O3 -fno-fast-math -ffp-contract=off lms_adaptive_filter_target.c -lm -o lms_filter
 *
 * Run:
 *   ./lms_filter [nsamples] [ntaps] [iters] [input_mode] [system_mode] [mu] [seed]
 *
 * Examples:
 *   ./lms_filter
 *   ./lms_filter 200000 64 3 0 0 0.01 1
 *   ./lms_filter 500000 128 3 2 2 0.002 1
 *
 * input_mode:
 *   0: white random input in [-1, 1]
 *   1: multi-tone sine input
 *   2: colored AR-like input
 *   3: step + small noise
 *   4: impulse + decaying sinusoid
 *
 * system_mode:
 *   0: sparse echo path
 *   1: exponentially decaying FIR
 *   2: alternating-sign cancellation-heavy FIR
 *   3: deterministic pseudo-random FIR
 */

#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif

#ifndef RESTRICT
#  if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    define RESTRICT restrict
#  else
#    define RESTRICT
#  endif
#endif

typedef struct {
    double mse;
    double checksum_y;
    double checksum_e;
    double output_energy;
    double error_energy;
    double weight_l2;
    double weight_error_l2;
    double max_abs_error;
} LmsSummary;

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

static double rng_uniform01(uint64_t *state) {
    return (double)(splitmix64(state) >> 11) * (1.0 / 9007199254740992.0);
}

static double rng_uniform_pm1(uint64_t *state) {
    return 2.0 * rng_uniform01(state) - 1.0;
}

static void *xmalloc_array(size_t count, size_t elem_size, const char *name) {
    if (count != 0 && elem_size > SIZE_MAX / count) {
        fprintf(stderr, "Allocation size overflow for %s\n", name);
        exit(EXIT_FAILURE);
    }

    void *ptr = malloc(count * elem_size);
    if (!ptr) {
        fprintf(stderr, "malloc failed for %s\n", name);
        exit(EXIT_FAILURE);
    }

    return ptr;
}

static void zero_array(double *x, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        x[i] = 0.0;
    }
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 1
 * ============================================================
 *
 * FIR prediction:
 *
 *   y = sum_i w[i] * x_vec[i]
 *
 * Precision-sensitive variables:
 *   - acc
 *   - product
 *   - w[i]
 *   - x_vec[i]
 */
double lms_predict(const double * RESTRICT w,
                   const double * RESTRICT x_vec,
                   int N) {
    double acc = 0.0;

    for (int i = 0; i < N; ++i) {
        double wi = w[i];
        double xi = x_vec[i];
        double product = wi * xi;
        acc += product;
    }

    return acc;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 2
 * ============================================================
 *
 * LMS update:
 *
 *   w[i] += 2 * mu * e * x[i]
 *
 * Precision-sensitive variables:
 *   - mu
 *   - e
 *   - scale = 2*mu*e
 *   - delta
 *   - w[i]
 */
void lms_update(double * RESTRICT w,
                const double * RESTRICT x_vec,
                double e,
                double mu,
                int N) {
    double scale = 2.0 * mu * e;

    for (int i = 0; i < N; ++i) {
        double xi = x_vec[i];
        double delta = scale * xi;
        w[i] += delta;
    }
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 3
 * ============================================================
 *
 * Complete LMS processing loop.
 *
 * This loop carries state through:
 *   - x_vec delay line
 *   - adaptive weights w[]
 */
void lms_process(const double * RESTRICT input,
                 const double * RESTRICT desired,
                 double * RESTRICT output,
                 double * RESTRICT error,
                 double * RESTRICT w,
                 double * RESTRICT x_vec,
                 size_t nsamples,
                 int N,
                 double mu) {
    for (size_t n = 0; n < nsamples; ++n) {
        /*
         * Update delay line:
         *   x_vec[0] = x[n]
         *   x_vec[1] = x[n-1]
         *   ...
         */
        for (int i = N - 1; i > 0; --i) {
            x_vec[i] = x_vec[i - 1];
        }
        x_vec[0] = input[n];

        double y = lms_predict(w, x_vec, N);
        double e = desired[n] - y;

        lms_update(w, x_vec, e, mu, N);

        output[n] = y;
        error[n] = e;
    }
}

static void fill_input(double *x, size_t nsamples, int input_mode, uint64_t seed, double fs) {
    uint64_t rng = seed + UINT64_C(0x6a09e667f3bcc909);
    double ar_state = 0.0;

    for (size_t n = 0; n < nsamples; ++n) {
        double v = 0.0;
        double t = (double)n / fs;

        switch (input_mode) {
            case 0:
                v = rng_uniform_pm1(&rng);
                break;

            case 1:
                v = 0.60 * sin(2.0 * M_PI * 440.0  * t)
                  + 0.30 * sin(2.0 * M_PI * 1200.0 * t)
                  + 0.15 * sin(2.0 * M_PI * 3100.0 * t);
                break;

            case 2: {
                /*
                 * Colored AR-like input. This creates correlated input vectors,
                 * which can slow LMS convergence and make mu precision more visible.
                 */
                double u = rng_uniform_pm1(&rng);
                ar_state = 0.92 * ar_state + 0.20 * u;
                v = ar_state;
                break;
            }

            case 3:
                v = 1.0 + 1.0e-6 * rng_uniform_pm1(&rng);
                break;

            case 4: {
                double decay = exp(-3.0 * t);
                v = ((n == 0) ? 1.0 : 0.0)
                  + 0.5 * decay * sin(2.0 * M_PI * 1000.0 * t);
                break;
            }

            default:
                fprintf(stderr, "Unsupported input_mode=%d\n", input_mode);
                exit(EXIT_FAILURE);
        }

        x[n] = v;
    }
}

static void init_unknown_system(double *h, int N, int system_mode, uint64_t seed) {
    uint64_t rng = seed + UINT64_C(0xbb67ae8584caa73b);

    for (int i = 0; i < N; ++i) {
        h[i] = 0.0;
    }

    switch (system_mode) {
        case 0:
            /*
             * Sparse echo path.
             * Several delayed echoes with decreasing magnitudes.
             */
            if (N > 0) h[0] = 0.70;
            if (N > 3) h[3] = -0.35;
            if (N > 8) h[8] = 0.25;
            if (N > 17) h[17] = -0.18;
            if (N > 31) h[31] = 0.10;
            if (N > 47) h[47] = -0.06;
            if (N > 63) h[63] = 0.04;
            break;

        case 1:
            /*
             * Exponentially decaying FIR.
             */
            for (int i = 0; i < N; ++i) {
                double sign = (i & 1) ? -1.0 : 1.0;
                h[i] = sign * exp(-0.06 * (double)i) * 0.25;
            }
            break;

        case 2:
            /*
             * Alternating-sign cancellation-heavy FIR.
             */
            for (int i = 0; i < N; ++i) {
                double amp = 0.15 / sqrt((double)i + 1.0);
                h[i] = (i & 1) ? -amp : amp;
            }
            break;

        case 3:
            /*
             * Deterministic pseudo-random FIR, normalized roughly by sqrt(N).
             */
            for (int i = 0; i < N; ++i) {
                h[i] = 0.5 * rng_uniform_pm1(&rng) / sqrt((double)N);
            }
            break;

        default:
            fprintf(stderr, "Unsupported system_mode=%d\n", system_mode);
            exit(EXIT_FAILURE);
    }
}

static void generate_desired_signal(const double *input,
                                    const double *h_true,
                                    double *desired,
                                    size_t nsamples,
                                    int N,
                                    uint64_t seed) {
    uint64_t rng = seed + UINT64_C(0x3c6ef372fe94f82b);
    double *delay = (double *)xmalloc_array((size_t)N, sizeof(double), "desired_delay");
    zero_array(delay, (size_t)N);

    for (size_t n = 0; n < nsamples; ++n) {
        for (int i = N - 1; i > 0; --i) {
            delay[i] = delay[i - 1];
        }
        delay[0] = input[n];

        double d = 0.0;
        for (int i = 0; i < N; ++i) {
            d += h_true[i] * delay[i];
        }

        /*
         * Very small observation noise prevents the problem from being
         * perfectly deterministic while keeping convergence measurable.
         */
        d += 1.0e-8 * rng_uniform_pm1(&rng);

        desired[n] = d;
    }

    free(delay);
}

static LmsSummary summarize_result(const double *output,
                                   const double *error,
                                   const double *w,
                                   const double *h_true,
                                   size_t nsamples,
                                   int N) {
    LmsSummary s;
    s.mse = 0.0;
    s.checksum_y = 0.0;
    s.checksum_e = 0.0;
    s.output_energy = 0.0;
    s.error_energy = 0.0;
    s.weight_l2 = 0.0;
    s.weight_error_l2 = 0.0;
    s.max_abs_error = 0.0;

    for (size_t n = 0; n < nsamples; ++n) {
        double y = (double)output[n];
        double e = (double)error[n];
        double weight = 1.0 + (double)(n % 251) * 1.0e-6;

        s.checksum_y += y * weight;
        s.checksum_e += e * weight;
        s.output_energy += y * y;
        s.error_energy += e * e;

        double ae = fabs(error[n]);
        if (ae > s.max_abs_error) {
            s.max_abs_error = ae;
        }
    }

    s.mse = s.error_energy / (double)nsamples;

    for (int i = 0; i < N; ++i) {
        double wi = (double)w[i];
        double diff = (double)w[i] - (double)h_true[i];
        s.weight_l2 += wi * wi;
        s.weight_error_l2 += diff * diff;
    }

    s.weight_l2 = sqrt(s.weight_l2);
    s.weight_error_l2 = sqrt(s.weight_error_l2);

    return s;
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

static double parse_double_arg(const char *s, const char *name, double min_value) {
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);

    if (errno != 0 || end == s || *end != '\0' || !isfinite(v) || v < min_value) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    return v;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [nsamples] [ntaps] [iters] [input_mode] [system_mode] [mu] [seed]\n"
            "\n"
            "Defaults: nsamples=200000 ntaps=64 iters=3 input_mode=0 system_mode=0 mu=0.01 seed=1\n"
            "\n"
            "input_mode:\n"
            "  0: white random input in [-1, 1]\n"
            "  1: multi-tone sine input\n"
            "  2: colored AR-like input\n"
            "  3: step + small noise\n"
            "  4: impulse + decaying sinusoid\n"
            "\n"
            "system_mode:\n"
            "  0: sparse echo path\n"
            "  1: exponentially decaying FIR\n"
            "  2: alternating-sign cancellation-heavy FIR\n"
            "  3: deterministic pseudo-random FIR\n",
            prog);
}

int main(int argc, char **argv) {
    int nsamples_arg = 200000;
    int ntaps = 64;
    int iters = 3;
    int input_mode = 0;
    int system_mode = 0;
    double mu = 0.01;
    int seed = 1;

    const double fs = 48000.0;

    if (argc > 8) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) nsamples_arg = parse_int_arg(argv[1], "nsamples", 1);
    if (argc >= 3) ntaps        = parse_int_arg(argv[2], "ntaps", 1);
    if (argc >= 4) iters        = parse_int_arg(argv[3], "iters", 1);
    if (argc >= 5) input_mode   = parse_int_arg(argv[4], "input_mode", 0);
    if (argc >= 6) system_mode  = parse_int_arg(argv[5], "system_mode", 0);
    if (argc >= 7) mu           = parse_double_arg(argv[6], "mu", 0.0);
    if (argc >= 8) seed         = parse_int_arg(argv[7], "seed", 0);

    if (input_mode < 0 || input_mode > 4) {
        fprintf(stderr, "input_mode must be in [0, 4]\n");
        return EXIT_FAILURE;
    }

    if (system_mode < 0 || system_mode > 3) {
        fprintf(stderr, "system_mode must be in [0, 3]\n");
        return EXIT_FAILURE;
    }

    const size_t nsamples = (size_t)nsamples_arg;

    double *input   = (double *)xmalloc_array(nsamples, sizeof(double), "input");
    double *desired = (double *)xmalloc_array(nsamples, sizeof(double), "desired");
    double *output  = (double *)xmalloc_array(nsamples, sizeof(double), "output");
    double *error   = (double *)xmalloc_array(nsamples, sizeof(double), "error");
    double *w       = (double *)xmalloc_array((size_t)ntaps, sizeof(double), "w");
    double *x_vec   = (double *)xmalloc_array((size_t)ntaps, sizeof(double), "x_vec");
    double *h_true  = (double *)xmalloc_array((size_t)ntaps, sizeof(double), "h_true");

    fill_input(input, nsamples, input_mode, (uint64_t)seed, fs);
    init_unknown_system(h_true, ntaps, system_mode, (uint64_t)seed);
    generate_desired_signal(input, h_true, desired, nsamples, ntaps, (uint64_t)seed);

    double total_time = 0.0;

    for (int it = 0; it < iters; ++it) {
        zero_array(w, (size_t)ntaps);
        zero_array(x_vec, (size_t)ntaps);

        const double t0 = wall_time_seconds();
        lms_process(input, desired, output, error, w, x_vec, nsamples, ntaps, mu);
        const double t1 = wall_time_seconds();

        total_time += (t1 - t0);
    }

    const double avg_time = total_time / (double)iters;

    /*
     * Approximate FLOP count:
     *   prediction: N multiplications + N additions
     *   e = d-y: 1 subtraction
     *   update: scale = 2*mu*e -> 2 multiplications
     *           per tap: delta = scale*x[i], w[i] += delta -> 1 mul + 1 add
     *
     * Total ≈ (2N) + 1 + 2 + (2N) = 4N + 3 FLOPs/sample
     */
    const double flops = ((4.0 * (double)ntaps) + 3.0) * (double)nsamples;
    const double gflops = flops / avg_time / 1.0e9;

    const LmsSummary summary = summarize_result(output, error, w, h_true, nsamples, ntaps);

    printf("benchmark = lms_adaptive_filter\n");
    printf("nsamples = %zu\n", nsamples);
    printf("ntaps = %d\n", ntaps);
    printf("iters = %d\n", iters);
    printf("input_mode = %d\n", input_mode);
    printf("system_mode = %d\n", system_mode);
    printf("seed = %d\n", seed);
    printf("sample_rate = %.17g\n", fs);
    printf("mu = %.17g\n", mu);
    printf("precision = double\n");
    printf("sizeof_double = %zu\n", sizeof(double));
    printf("DBL_MANT_DIG = %d\n", DBL_MANT_DIG);
    printf("DBL_DIG = %d\n", DBL_DIG);
    printf("avg_kernel_time_sec = %.9f\n", avg_time);
    printf("gflops_est = %.9f\n", gflops);
    printf("mse = %.21e\n", summary.mse);
    printf("max_abs_error = %.17g\n", summary.max_abs_error);
    printf("checksum_y = %.21e\n", summary.checksum_y);
    printf("checksum_e = %.21e\n", summary.checksum_e);
    printf("output_energy = %.21e\n", summary.output_energy);
    printf("error_energy = %.21e\n", summary.error_energy);
    printf("weight_l2 = %.21e\n", summary.weight_l2);
    printf("weight_error_l2 = %.21e\n", summary.weight_error_l2);

    free(input);
    free(desired);
    free(output);
    free(error);
    free(w);
    free(x_vec);
    free(h_true);

    return EXIT_SUCCESS;
}
