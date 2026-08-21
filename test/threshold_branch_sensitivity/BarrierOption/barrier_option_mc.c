// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize -no-build "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/*
 * barrier_option_mc_target.c
 *
 * Barrier option Monte Carlo pricing benchmark for mixed-precision tuning tools.
 *
 * Model:
 *   Geometric Brownian Motion:
 *
 *     dS = r*S*dt + sigma*S*dW
 *
 * Discrete path update:
 *
 *     S_{t+1} = S_t * exp((r - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z_t)
 *
 * Barrier option:
 *   The payoff depends on whether the path crosses a barrier B.
 *
 * Supported barrier modes:
 *   barrier_mode = 0: up-and-out
 *   barrier_mode = 1: down-and-out
 *   barrier_mode = 2: up-and-in
 *   barrier_mode = 3: down-and-in
 *
 * Supported option types:
 *   option_type = 0: call
 *   option_type = 1: put
 *
 * Purpose:
 *   This program exposes classic mixed-precision issues in path-dependent
 *   Monte Carlo option pricing:
 *     - stochastic recursive state S
 *     - exp() multiplicative path update
 *     - barrier crossing branch sensitivity
 *     - max/min path extrema sensitivity
 *     - payoff branch max(S-K,0) or max(K-S,0)
 *     - large payoff reduction across paths
 *
 * Intended precision-search targets:
 *   - rng_uniform01()
 *   - gaussian_random()
 *   - barrier_is_crossed()
 *   - simulate_barrier_payoff()
 *   - price_barrier_MC()
 *
 * This file intentionally does not include a high-precision reference or full
 * error metrics. Use an external harness to compare the original baseline and
 * the mixed-precision transformed version.
 *
 * Build:
 *   gcc   -std=c99 -O3 -fno-fast-math -ffp-contract=off barrier_option_mc_target.c -lm -o barrier_mc
 *   clang -std=c99 -O3 -fno-fast-math -ffp-contract=off barrier_option_mc_target.c -lm -o barrier_mc
 *
 * Run:
 *   ./barrier_mc [S0] [K] [B] [r] [sigma] [T] [N_steps] [N_paths] [iters] [barrier_mode] [option_type] [seed]
 *
 * Examples:
 *   ./barrier_mc
 *   ./barrier_mc 100 100 120 0.05 0.20 1.0 252 200000 3 0 0 1
 *   ./barrier_mc 100 100 80  0.05 0.20 1.0 252 200000 3 1 1 1
 *
 * Notes:
 *   - Barrier monitoring is discrete at each simulated time step.
 *   - The reported stderr_price is Monte Carlo sampling error, not numerical error.
 *   - Keep seed, N_steps, N_paths, barrier_mode, and option_type fixed for comparisons.
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
    uint64_t state;
    int has_spare;
    double spare;
} RNGState;

typedef struct {
    double payoff;
    double final_S;
    double path_min;
    double path_max;
    int crossed;
} BarrierPathResult;

typedef struct {
    double price;
    double mean_payoff;
    double stderr_price;
    double payoff_variance;
    double hit_ratio;
    double mean_final_S;
    double mean_path_min;
    double mean_path_max;
    double payoff_sum;
    double payoff_sq_sum;
    double checksum_payoff;
    double final_S_sum;
    double path_min_sum;
    double path_max_sum;
    long long crossed_count;
} BarrierMCResult;

static double wall_time_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
    }
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 1
 * ============================================================
 *
 * Uniform RNG in (0,1). Avoid exactly 0 because Box-Muller uses log(u1).
 */
double rng_uniform01(RNGState *rng) {
    uint64_t bits = splitmix64_next(&rng->state) >> 11;
    double u = ((double)bits + 0.5) * (1.0 / 9007199254740992.0);

    if (u <= 0.0) {
        u = DBL_MIN;
    }
    if (u >= 1.0) {
        u = 1.0 - DBL_EPSILON;
    }

    return u;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 2
 * ============================================================
 *
 * Standard normal RNG using Box-Muller transform.
 */
double gaussian_random(RNGState *rng) {
    if (rng->has_spare) {
        rng->has_spare = 0;
        return rng->spare;
    }

    double u1 = rng_uniform01(rng);
    double u2 = rng_uniform01(rng);

    double radius = sqrt(-2.0 * log(u1));
    double theta = 2.0 * M_PI * u2;

    double z0 = radius * cos(theta);
    double z1 = radius * sin(theta);

    rng->spare = z1;
    rng->has_spare = 1;

    return z0;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 3
 * ============================================================
 *
 * Barrier crossing predicate.
 *
 * barrier_mode:
 *   0 = up-and-out
 *   1 = down-and-out
 *   2 = up-and-in
 *   3 = down-and-in
 *
 * The crossing condition is shared by in/out variants:
 *   up barrier:   S >= B
 *   down barrier: S <= B
 */
int barrier_is_crossed(double S, double B, int barrier_mode) {
    if (barrier_mode == 0 || barrier_mode == 2) {
        return S >= B;
    }

    if (barrier_mode == 1 || barrier_mode == 3) {
        return S <= B;
    }

    fprintf(stderr, "Unsupported barrier_mode=%d\n", barrier_mode);
    exit(EXIT_FAILURE);
}

/*
 * Plain European payoff.
 *
 * option_type:
 *   0 = call
 *   1 = put
 */
double european_payoff(double S, double K, int option_type) {
    if (option_type == 0) {
        return fmax(S - K, 0.0);
    }

    if (option_type == 1) {
        return fmax(K - S, 0.0);
    }

    fprintf(stderr, "Unsupported option_type=%d\n", option_type);
    exit(EXIT_FAILURE);
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 4
 * ============================================================
 *
 * Simulate one discretely monitored barrier option payoff.
 *
 * Precision-sensitive variables:
 *   - Z
 *   - exponent_arg
 *   - growth
 *   - S
 *   - path_min/path_max
 *   - crossed branch
 *   - active branch for in/out payoff
 *   - intrinsic payoff near S-K or K-S
 */
BarrierPathResult simulate_barrier_payoff(double S0,
                                          double K,
                                          double B,
                                          double drift,
                                          double vol_sqrt_dt,
                                          int N_steps,
                                          int barrier_mode,
                                          int option_type,
                                          RNGState *rng) {
    BarrierPathResult path;

    double S = S0;
    double path_min = S0;
    double path_max = S0;
    int crossed = barrier_is_crossed(S0, B, barrier_mode);

    for (int t = 0; t < N_steps; ++t) {
        double Z = gaussian_random(rng);
        double exponent_arg = drift + vol_sqrt_dt * Z;
        double growth = exp(exponent_arg);

        S *= growth;

        if (S < path_min) {
            path_min = S;
        }
        if (S > path_max) {
            path_max = S;
        }

        if (!crossed && barrier_is_crossed(S, B, barrier_mode)) {
            crossed = 1;
        }
    }

    double vanilla = european_payoff(S, K, option_type);

    int is_out = (barrier_mode == 0 || barrier_mode == 1);
    int active;

    if (is_out) {
        active = !crossed;
    } else {
        active = crossed;
    }

    path.payoff = active ? vanilla : 0.0;
    path.final_S = S;
    path.path_min = path_min;
    path.path_max = path_max;
    path.crossed = crossed;

    return path;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 5
 * ============================================================
 *
 * Monte Carlo pricing for a barrier option.
 *
 * Precision-sensitive variables:
 *   - dt
 *   - discount
 *   - drift
 *   - vol_sqrt_dt
 *   - payoff_sum
 *   - payoff_sq_sum
 *   - crossed_count
 */
BarrierMCResult price_barrier_MC(double S0,
                                 double K,
                                 double B,
                                 double r,
                                 double sigma,
                                 double T,
                                 int N_steps,
                                 int N_paths,
                                 int barrier_mode,
                                 int option_type,
                                 uint64_t seed) {
    BarrierMCResult result;

    double dt = T / (double)N_steps;
    double discount = exp(-r * T);
    double drift = (r - 0.5 * sigma * sigma) * dt;
    double vol_sqrt_dt = sigma * sqrt(dt);

    double payoff_sum = 0.0;
    double payoff_sq_sum = 0.0;
    double checksum_payoff = 0.0;
    double final_S_sum = 0.0;
    double path_min_sum = 0.0;
    double path_max_sum = 0.0;
    long long crossed_count = 0;

    RNGState rng;
    rng.state = seed + UINT64_C(0x9e3779b97f4a7c15);
    rng.has_spare = 0;
    rng.spare = 0.0;

    for (int p = 0; p < N_paths; ++p) {
        BarrierPathResult path = simulate_barrier_payoff(S0, K, B,
                                                         drift, vol_sqrt_dt,
                                                         N_steps,
                                                         barrier_mode,
                                                         option_type,
                                                         &rng);

        double payoff_ld = (double)path.payoff;
        payoff_sum += payoff_ld;
        payoff_sq_sum += payoff_ld * payoff_ld;

        final_S_sum += (double)path.final_S;
        path_min_sum += (double)path.path_min;
        path_max_sum += (double)path.path_max;
        crossed_count += (long long)path.crossed;

        /*
         * Weighted checksum reduces the chance that differences are hidden by
         * simple summation cancellation.
         */
        double weight = 1.0 + (double)(p % 251) * 1.0e-6;
        checksum_payoff += payoff_ld * weight;
    }

    double mean_payoff_ld = payoff_sum / (double)N_paths;
    double mean_sq_payoff_ld = payoff_sq_sum / (double)N_paths;
    double variance_ld = mean_sq_payoff_ld - mean_payoff_ld * mean_payoff_ld;

    if (variance_ld < 0.0 && fabs(variance_ld) < 1.0e-24) {
        variance_ld = 0.0;
    }

    double mean_payoff = (double)mean_payoff_ld;
    double payoff_variance = (double)variance_ld;
    double stderr_price = discount * sqrt(fmax(payoff_variance, 0.0) / (double)N_paths);
    double price = discount * mean_payoff;

    result.price = price;
    result.mean_payoff = mean_payoff;
    result.stderr_price = stderr_price;
    result.payoff_variance = payoff_variance;
    result.hit_ratio = (double)crossed_count / (double)N_paths;
    result.mean_final_S = (double)(final_S_sum / (double)N_paths);
    result.mean_path_min = (double)(path_min_sum / (double)N_paths);
    result.mean_path_max = (double)(path_max_sum / (double)N_paths);
    result.payoff_sum = payoff_sum;
    result.payoff_sq_sum = payoff_sq_sum;
    result.checksum_payoff = checksum_payoff;
    result.final_S_sum = final_S_sum;
    result.path_min_sum = path_min_sum;
    result.path_max_sum = path_max_sum;
    result.crossed_count = crossed_count;

    return result;
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

static uint64_t parse_u64_arg(const char *s, const char *name) {
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    return (uint64_t)v;
}

static double parse_double_arg(const char *s, const char *name, double min_value, int strict_greater) {
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);

    if (errno != 0 || end == s || *end != '\0' || !isfinite(v)) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    if (strict_greater) {
        if (v <= min_value) {
            fprintf(stderr, "Invalid %s: %s\n", name, s);
            exit(EXIT_FAILURE);
        }
    } else {
        if (v < min_value) {
            fprintf(stderr, "Invalid %s: %s\n", name, s);
            exit(EXIT_FAILURE);
        }
    }

    return v;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [S0] [K] [B] [r] [sigma] [T] [N_steps] [N_paths] [iters] [barrier_mode] [option_type] [seed]\n"
            "\n"
            "Defaults:\n"
            "  S0=100 K=100 B=120 r=0.05 sigma=0.20 T=1.0 N_steps=252 N_paths=200000 iters=3 barrier_mode=0 option_type=0 seed=1\n"
            "\n"
            "barrier_mode:\n"
            "  0: up-and-out\n"
            "  1: down-and-out\n"
            "  2: up-and-in\n"
            "  3: down-and-in\n"
            "\n"
            "option_type:\n"
            "  0: call\n"
            "  1: put\n"
            "\n"
            "Examples:\n"
            "  %s\n"
            "  %s 100 100 120 0.05 0.20 1.0 252 200000 3 0 0 1\n"
            "  %s 100 100 80  0.05 0.20 1.0 252 200000 3 1 1 1\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    double S0 = 100.0;
    double K = 100.0;
    double B = 120.0;
    double r = 0.05;
    double sigma = 0.20;
    double T = 1.0;
    int N_steps = 252;
    int N_paths = 200000;
    int iters = 3;
    int barrier_mode = 0;
    int option_type = 0;
    uint64_t seed = 1;

    if (argc > 13) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2)  S0           = parse_double_arg(argv[1], "S0", 0.0, 1);
    if (argc >= 3)  K            = parse_double_arg(argv[2], "K", 0.0, 0);
    if (argc >= 4)  B            = parse_double_arg(argv[3], "B", 0.0, 1);
    if (argc >= 5)  r            = parse_double_arg(argv[4], "r", -1.0e100, 0);
    if (argc >= 6)  sigma        = parse_double_arg(argv[5], "sigma", 0.0, 0);
    if (argc >= 7)  T            = parse_double_arg(argv[6], "T", 0.0, 1);
    if (argc >= 8)  N_steps      = parse_int_arg(argv[7], "N_steps", 1);
    if (argc >= 9)  N_paths      = parse_int_arg(argv[8], "N_paths", 1);
    if (argc >= 10) iters        = parse_int_arg(argv[9], "iters", 1);
    if (argc >= 11) barrier_mode = parse_int_arg(argv[10], "barrier_mode", 0);
    if (argc >= 12) option_type  = parse_int_arg(argv[11], "option_type", 0);
    if (argc >= 13) seed         = parse_u64_arg(argv[12], "seed");

    if (barrier_mode < 0 || barrier_mode > 3) {
        fprintf(stderr, "barrier_mode must be in [0, 3]\n");
        return EXIT_FAILURE;
    }

    if (option_type < 0 || option_type > 1) {
        fprintf(stderr, "option_type must be 0 or 1\n");
        return EXIT_FAILURE;
    }

    BarrierMCResult final_result;
    double total_time = 0.0;

    for (int it = 0; it < iters; ++it) {
        uint64_t iter_seed = seed + (uint64_t)it * UINT64_C(0x9e3779b97f4a7c15);

        const double t0 = wall_time_seconds();
        final_result = price_barrier_MC(S0, K, B, r, sigma, T,
                                        N_steps, N_paths,
                                        barrier_mode, option_type,
                                        iter_seed);
        const double t1 = wall_time_seconds();

        total_time += (t1 - t0);
    }

    double avg_time = total_time / (double)iters;

    /*
     * Approximate operations per path step:
     *   Gaussian RNG, exp path update, min/max tracking, barrier branch.
     * This is only a relative throughput estimate for comparing transformed versions.
     */
    double ops_est = (8.0 * (double)N_steps + 10.0) * (double)N_paths;
    double gops_est = ops_est / avg_time / 1.0e9;

    printf("benchmark = barrier_option_monte_carlo\n");
    printf("S0 = %.17g\n", S0);
    printf("K = %.17g\n", K);
    printf("B = %.17g\n", B);
    printf("r = %.17g\n", r);
    printf("sigma = %.17g\n", sigma);
    printf("T = %.17g\n", T);
    printf("N_steps = %d\n", N_steps);
    printf("N_paths = %d\n", N_paths);
    printf("iters = %d\n", iters);
    printf("barrier_mode = %d\n", barrier_mode);
    printf("option_type = %d\n", option_type);
    printf("seed = %llu\n", (unsigned long long)seed);
    printf("precision = double\n");
    printf("sizeof_double = %zu\n", sizeof(double));
    printf("DBL_MANT_DIG = %d\n", DBL_MANT_DIG);
    printf("DBL_DIG = %d\n", DBL_DIG);
    printf("avg_kernel_time_sec = %.9f\n", avg_time);
    printf("gops_est = %.9f\n", gops_est);
    printf("price = %.17g\n", final_result.price);
    printf("mean_payoff = %.17g\n", final_result.mean_payoff);
    printf("stderr_price = %.17g\n", final_result.stderr_price);
    printf("payoff_variance = %.17g\n", final_result.payoff_variance);
    printf("hit_ratio = %.17g\n", final_result.hit_ratio);
    printf("crossed_count = %lld\n", final_result.crossed_count);
    printf("mean_final_S = %.17g\n", final_result.mean_final_S);
    printf("mean_path_min = %.17g\n", final_result.mean_path_min);
    printf("mean_path_max = %.17g\n", final_result.mean_path_max);
    printf("payoff_sum = %.21e\n", final_result.payoff_sum);
    printf("payoff_sq_sum = %.21e\n", final_result.payoff_sq_sum);
    printf("final_S_sum = %.21e\n", final_result.final_S_sum);
    printf("path_min_sum = %.21e\n", final_result.path_min_sum);
    printf("path_max_sum = %.21e\n", final_result.path_max_sum);
    printf("checksum_payoff = %.21e\n", final_result.checksum_payoff);

    return EXIT_SUCCESS;
}
