// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/*
 * biquad_df2t_target.c
 *
 * IIR Biquad filter benchmark for mixed-precision tuning tools.
 *
 * Mathematical structure:
 *
 *   H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
 *
 * Direct Form II Transposed update:
 *
 *   y  = b0*x + w1
 *   w1 = b1*x - a1*y + w2
 *   w2 = b2*x - a2*y
 *
 * Purpose:
 *   This program exposes the classic DSP precision-sensitive pattern:
 *     - recursive state variables w1/w2
 *     - feedback coefficients a1/a2
 *     - cancellation in b1*x - a1*y + w2
 *     - long time-series error accumulation
 *
 *   The intended precision-search target is biquad_df2t() and, optionally,
 *   process_signal().
 *
 * Build:
 *   gcc   -std=c99 -O3 -fno-fast-math -ffp-contract=off biquad_df2t_target.c -lm -o biquad_df2t
 *   clang -std=c99 -O3 -fno-fast-math -ffp-contract=off biquad_df2t_target.c -lm -o biquad_df2t
 *
 * Run:
 *   ./biquad_df2t [nsamples] [iters] [input_mode] [filter_mode] [seed]
 *
 * Examples:
 *   ./biquad_df2t
 *   ./biquad_df2t 1000000 3 0 0 1
 *   ./biquad_df2t 1000000 3 2 2 1
 *
 * input_mode:
 *   0: multi-tone sine input
 *   1: impulse input
 *   2: step + small noise
 *   3: pseudo-random in [-1, 1]
 *   4: decaying sinusoid
 *
 * filter_mode:
 *   0: RBJ low-pass, moderate Q
 *   1: RBJ high-pass, moderate Q
 *   2: narrow notch, high Q
 *   3: resonator-like band-pass, high Q
 *
 * Notes:
 *   This file intentionally does not include a high-precision reference or
 *   error metrics. Use an external harness to compare the original baseline
 *   and the mixed-precision transformed version.
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

typedef struct {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
    double w1;
    double w2;
} Biquad;

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

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 1
 * ============================================================
 *
 * This is the classic Direct Form II Transposed Biquad update.
 *
 * Important precision-sensitive variables:
 *   - y
 *   - new_w1
 *   - new_w2
 *   - *w1
 *   - *w2
 *   - feedback terms a1*y and a2*y
 */
double biquad_df2t(double x,
                   double b0, double b1, double b2,
                   double a1, double a2,
                   double *w1, double *w2) {
    double y = b0 * x + *w1;

    double feedforward_1 = b1 * x;
    double feedback_1    = a1 * y;
    double new_w1        = feedforward_1 - feedback_1 + *w2;

    double feedforward_2 = b2 * x;
    double feedback_2    = a2 * y;
    double new_w2        = feedforward_2 - feedback_2;

    *w1 = new_w1;
    *w2 = new_w2;

    return y;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 2, optional
 * ============================================================
 *
 * The loop carries recursive state through q->w1/q->w2.
 */
void process_signal(const double *input, double *output, size_t nsamples, Biquad *q) {
    for (size_t n = 0; n < nsamples; ++n) {
        output[n] = biquad_df2t(input[n],
                                q->b0, q->b1, q->b2,
                                q->a1, q->a2,
                                &q->w1, &q->w2);
    }
}

static void biquad_reset(Biquad *q) {
    q->w1 = 0.0;
    q->w2 = 0.0;
}

static void biquad_set_normalized(Biquad *q,
                                  double b0, double b1, double b2,
                                  double a0, double a1, double a2) {
    if (a0 == 0.0) {
        fprintf(stderr, "invalid biquad: a0 is zero\n");
        exit(EXIT_FAILURE);
    }

    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
    q->w1 = 0.0;
    q->w2 = 0.0;
}

static void init_biquad(Biquad *q, int filter_mode, double fs) {
    /*
     * RBJ cookbook-style coefficients for common filters.
     * The program stores the denominator as 1 + a1*z^-1 + a2*z^-2,
     * matching the user-provided formula.
     */
    double f0 = 1000.0;
    double Q  = 0.7071067811865476;
    double w0 = 0.0;
    double cw = 0.0;
    double sw = 0.0;
    double alpha = 0.0;

    switch (filter_mode) {
        case 0:
            /* Low-pass, moderate Q. */
            f0 = 1000.0;
            Q  = 0.7071067811865476;
            w0 = 2.0 * M_PI * f0 / fs;
            cw = cos(w0);
            sw = sin(w0);
            alpha = sw / (2.0 * Q);
            biquad_set_normalized(q,
                                  (1.0 - cw) * 0.5,
                                  1.0 - cw,
                                  (1.0 - cw) * 0.5,
                                  1.0 + alpha,
                                  -2.0 * cw,
                                  1.0 - alpha);
            break;

        case 1:
            /* High-pass, moderate Q. */
            f0 = 1000.0;
            Q  = 0.7071067811865476;
            w0 = 2.0 * M_PI * f0 / fs;
            cw = cos(w0);
            sw = sin(w0);
            alpha = sw / (2.0 * Q);
            biquad_set_normalized(q,
                                  (1.0 + cw) * 0.5,
                                  -(1.0 + cw),
                                  (1.0 + cw) * 0.5,
                                  1.0 + alpha,
                                  -2.0 * cw,
                                  1.0 - alpha);
            break;

        case 2:
            /*
             * Narrow notch, high Q.
             * This stresses pole/zero cancellation and state precision.
             */
            f0 = 1200.0;
            Q  = 30.0;
            w0 = 2.0 * M_PI * f0 / fs;
            cw = cos(w0);
            sw = sin(w0);
            alpha = sw / (2.0 * Q);
            biquad_set_normalized(q,
                                  1.0,
                                  -2.0 * cw,
                                  1.0,
                                  1.0 + alpha,
                                  -2.0 * cw,
                                  1.0 - alpha);
            break;

        case 3:
            /*
             * Constant-skirt-gain band-pass, high Q.
             * This creates a long ringing response and is sensitive to
             * recursive state precision.
             */
            f0 = 1000.0;
            Q  = 20.0;
            w0 = 2.0 * M_PI * f0 / fs;
            cw = cos(w0);
            sw = sin(w0);
            alpha = sw / (2.0 * Q);
            biquad_set_normalized(q,
                                  sw * 0.5,
                                  0.0,
                                  -sw * 0.5,
                                  1.0 + alpha,
                                  -2.0 * cw,
                                  1.0 - alpha);
            break;

        default:
            fprintf(stderr, "Unsupported filter_mode=%d\n", filter_mode);
            exit(EXIT_FAILURE);
    }
}

static void fill_input(double *x, size_t nsamples, int input_mode, uint64_t seed, double fs) {
    uint64_t rng = seed + UINT64_C(0x86d1f9355d1719ad);

    for (size_t n = 0; n < nsamples; ++n) {
        const double t = (double)n / fs;
        double v = 0.0;

        switch (input_mode) {
            case 0:
                /*
                 * Multi-tone deterministic signal.
                 * Contains passband and stopband components.
                 */
                v = 0.60 * sin(2.0 * M_PI * 440.0  * t)
                  + 0.35 * sin(2.0 * M_PI * 1000.0 * t)
                  + 0.20 * sin(2.0 * M_PI * 3300.0 * t);
                break;

            case 1:
                /* Impulse response test. */
                v = (n == 0) ? 1.0 : 0.0;
                break;

            case 2:
                /*
                 * Step plus small noise.
                 * Useful for testing long recursive state settling.
                 */
                v = 1.0 + 1.0e-6 * rng_uniform_pm1(&rng);
                break;

            case 3:
                /* Wideband random input. */
                v = rng_uniform_pm1(&rng);
                break;

            case 4: {
                /*
                 * Decaying sinusoid close to the filter frequency.
                 * This stresses recursive state precision.
                 */
                const double decay = exp(-t * 2.0);
                v = decay * sin(2.0 * M_PI * 1000.0 * t);
                break;
            }

            default:
                fprintf(stderr, "Unsupported input_mode=%d\n", input_mode);
                exit(EXIT_FAILURE);
        }

        x[n] = v;
    }
}

static double checksum_signal(const double *y, size_t nsamples) {
    double checksum = 0.0;

    for (size_t n = 0; n < nsamples; ++n) {
        /*
         * Weighted checksum reduces the chance that signed errors cancel.
         */
        const double weight = 1.0 + (double)(n % 251) * 1.0e-6;
        checksum += (double)y[n] * weight;
    }

    return checksum;
}

static double signal_energy(const double *y, size_t nsamples) {
    double e = 0.0;

    for (size_t n = 0; n < nsamples; ++n) {
        const double v = (double)y[n];
        e += v * v;
    }

    return e;
}

static double max_abs_signal(const double *y, size_t nsamples) {
    double m = 0.0;

    for (size_t n = 0; n < nsamples; ++n) {
        const double a = fabs(y[n]);
        if (a > m) {
            m = a;
        }
    }

    return m;
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

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [nsamples] [iters] [input_mode] [filter_mode] [seed]\n"
            "\n"
            "Defaults: nsamples=1000000 iters=3 input_mode=0 filter_mode=0 seed=1\n"
            "\n"
            "input_mode:\n"
            "  0: multi-tone sine input\n"
            "  1: impulse input\n"
            "  2: step + small noise\n"
            "  3: pseudo-random in [-1, 1]\n"
            "  4: decaying sinusoid\n"
            "\n"
            "filter_mode:\n"
            "  0: RBJ low-pass, moderate Q\n"
            "  1: RBJ high-pass, moderate Q\n"
            "  2: narrow notch, high Q\n"
            "  3: resonator-like band-pass, high Q\n",
            prog);
}

int main(int argc, char **argv) {
    int nsamples_arg = 1000000;
    int iters = 3;
    int input_mode = 0;
    int filter_mode = 0;
    int seed = 1;

    const double fs = 48000.0;

    if (argc > 6) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) nsamples_arg = parse_int_arg(argv[1], "nsamples", 1);
    if (argc >= 3) iters        = parse_int_arg(argv[2], "iters", 1);
    if (argc >= 4) input_mode   = parse_int_arg(argv[3], "input_mode", 0);
    if (argc >= 5) filter_mode  = parse_int_arg(argv[4], "filter_mode", 0);
    if (argc >= 6) seed         = parse_int_arg(argv[5], "seed", 0);

    if (input_mode < 0 || input_mode > 4) {
        fprintf(stderr, "input_mode must be in [0, 4]\n");
        return EXIT_FAILURE;
    }

    if (filter_mode < 0 || filter_mode > 3) {
        fprintf(stderr, "filter_mode must be in [0, 3]\n");
        return EXIT_FAILURE;
    }

    const size_t nsamples = (size_t)nsamples_arg;
    double *x = (double *)xmalloc_array(nsamples, sizeof(double), "input");
    double *y = (double *)xmalloc_array(nsamples, sizeof(double), "output");

    fill_input(x, nsamples, input_mode, (uint64_t)seed, fs);

    Biquad q;
    init_biquad(&q, filter_mode, fs);

    double total_time = 0.0;

    for (int it = 0; it < iters; ++it) {
        biquad_reset(&q);

        const double t0 = wall_time_seconds();
        process_signal(x, y, nsamples, &q);
        const double t1 = wall_time_seconds();

        total_time += (t1 - t0);
    }

    const double avg_time = total_time / (double)iters;

    /*
     * Approximate FLOP count per sample:
     *   y:      b0*x + w1                  -> 1 mul + 1 add
     *   new_w1: b1*x - a1*y + w2           -> 2 mul + 2 add/sub
     *   new_w2: b2*x - a2*y                -> 2 mul + 1 sub
     * Total: 5 mul + 4 add/sub = 9 FLOPs/sample
     */
    const double flops = 9.0 * (double)nsamples;
    const double gflops = flops / avg_time / 1.0e9;

    const double checksum = checksum_signal(y, nsamples);
    const double energy = signal_energy(y, nsamples);
    const double max_abs = max_abs_signal(y, nsamples);

    printf("benchmark = iir_biquad_df2t\n");
    printf("nsamples = %zu\n", nsamples);
    printf("iters = %d\n", iters);
    printf("input_mode = %d\n", input_mode);
    printf("filter_mode = %d\n", filter_mode);
    printf("seed = %d\n", seed);
    printf("sample_rate = %.17g\n", fs);
    printf("precision = double\n");
    printf("sizeof_double = %zu\n", sizeof(double));
    printf("DBL_MANT_DIG = %d\n", DBL_MANT_DIG);
    printf("DBL_DIG = %d\n", DBL_DIG);
    printf("coeff_b0 = %.17g\n", q.b0);
    printf("coeff_b1 = %.17g\n", q.b1);
    printf("coeff_b2 = %.17g\n", q.b2);
    printf("coeff_a1 = %.17g\n", q.a1);
    printf("coeff_a2 = %.17g\n", q.a2);
    printf("final_w1 = %.17g\n", q.w1);
    printf("final_w2 = %.17g\n", q.w2);
    printf("avg_kernel_time_sec = %.9f\n", avg_time);
    printf("gflops_est = %.9f\n", gflops);
    printf("checksum = %.21e\n", checksum);
    printf("energy = %.21e\n", energy);
    printf("max_abs = %.17g\n", max_abs);

    free(x);
    free(y);

    return EXIT_SUCCESS;
}
