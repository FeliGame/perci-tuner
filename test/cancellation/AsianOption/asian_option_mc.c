// RUN: timeout -k 2 90 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -arg=100 -arg=100 -arg=0.05 -arg=0.20 -arg=1.0 -arg=10 -arg=2000 -arg=1 -arg=1 -include-func=simulate_path_payoff -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

/*
 * asian_option_mc_target.c
 *
 * Asian Option Monte Carlo pricing benchmark for mixed-precision tuning tools.
 *
 * Mathematical structure:
 *
 *   Geometric Brownian Motion:
 *     dS = r*S*dt + sigma*S*dW
 *
 *   Discrete path simulation:
 *     S_{t+1} = S_t * exp((r - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z_t)
 *
 *   Arithmetic-average Asian call payoff:
 *     payoff = max(avg(S_1, ..., S_N) - K, 0)
 *
 *   Option price:
 *     price = exp(-r*T) * mean(payoff)
 *
 * Purpose:
 *   This program exposes classic mixed-precision issues in Monte Carlo finance:
 *     - RNG and Gaussian transformation sensitivity
 *     - exp() path-update error
 *     - repeated multiplicative state update of S
 *     - accumulation of path average over N_steps
 *     - cancellation near avg - K
 *     - large payoff_sum reduction over N_paths
 *
 * Intended precision-search targets:
 *   - rng_uniform01()
 *   - gaussian_random()
 *   - simulate_path_payoff()
 *   - price_asian_MC()
 *
 * This file intentionally does not include a high-precision reference or full
 * error metrics. Use an external harness to compare the original baseline and
 * the mixed-precision transformed version.
 *
 * Build:
 *   gcc   -std=c99 -O3 -fno-fast-math -ffp-contract=off asian_option_mc_target.c -lm -o asian_mc
 *   clang -std=c99 -O3 -fno-fast-math -ffp-contract=off asian_option_mc_target.c -lm -o asian_mc
 *
 * Run:
 *   ./asian_mc [S0] [K] [r] [sigma] [T] [N_steps] [N_paths] [iters] [seed]
 *
 * Examples:
 *   ./asian_mc
 *   ./asian_mc 100 100 0.05 0.2 1.0 252 200000 3 1
 *   ./asian_mc 100 100 0.05 0.8 1.0 252 500000 3 1
 *
 * Notes:
 *   - The program uses a deterministic splitmix64 RNG and Box-Muller transform.
 *   - For fair mixed-precision comparisons, keep seed, N_steps, and N_paths fixed.
 *   - The reported standard error is Monte Carlo sampling error, not numerical error.
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
    double price;
    double mean_payoff;
    double stderr_price;
    double payoff_variance;
    double payoff_sum;
    double payoff_sq_sum;
    double checksum_payoff;
} AsianMCResult;

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
 *
 * Precision-sensitive point:
 *   - quality and precision of uniform random values
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
 * Gaussian RNG using Box-Muller transform.
 *
 * Precision-sensitive points:
 *   - log(u1)
 *   - sqrt(-2*log(u1))
 *   - sin/cos
 *   - cached spare normal value
 */
double gaussian_random(RNGState *rng) {
    if (rng->has_spare) {
        rng->has_spare = 0;
        return rng->spare;
    }

    double u1 = rng_uniform01(rng);
    double u2 = rng_uniform01(rng);

    double radius = sqrt(-2.0 * log(u1));
    double theta  = 2.0 * M_PI * u2;

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
 * Simulate one arithmetic-average Asian call payoff.
 *
 * Precision-sensitive variables:
 *   - drift
 *   - vol_sqrt_dt
 *   - Z
 *   - exponent_arg
 *   - growth
 *   - S
 *   - avg_sum
 *   - avg
 *   - intrinsic = avg - K
 */
double simulate_path_payoff(double S0,
                            double K,
                            double drift,
                            double vol_sqrt_dt,
                            int N_steps,
                            RNGState *rng) {
    double S = S0;
    double avg_sum = 0.0;

    for (int t = 0; t < N_steps; ++t) {
        double Z = gaussian_random(rng);
        double exponent_arg = drift + vol_sqrt_dt * Z;
        double growth = exp(exponent_arg);

        S *= growth;
        avg_sum += S;
    }

    double avg = avg_sum / (double)N_steps;
    double intrinsic = avg - K;
    double payoff = fmax(intrinsic, 0.0);

    return payoff;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 4
 * ============================================================
 *
 * Monte Carlo Asian option pricing.
 *
 * Precision-sensitive variables:
 *   - dt
 *   - discount
 *   - drift
 *   - vol_sqrt_dt
 *   - payoff_sum
 *   - payoff_sq_sum
 */
AsianMCResult price_asian_MC(double S0,
                             double K,
                             double r,
                             double sigma,
                             double T,
                             int N_steps,
                             int N_paths,
                             uint64_t seed) {
    AsianMCResult result;

    double dt = T / (double)N_steps;
    double discount = exp(-r * T);
    double drift = (r - 0.5 * sigma * sigma) * dt;
    double vol_sqrt_dt = sigma * sqrt(dt);

    double payoff_sum = 0.0;
    double payoff_sq_sum = 0.0;
    double checksum_payoff = 0.0;

    RNGState rng;
    rng.state = seed + UINT64_C(0x243f6a8885a308d3);
    rng.has_spare = 0;
    rng.spare = 0.0;

    for (int p = 0; p < N_paths; ++p) {
        double payoff = simulate_path_payoff(S0, K, drift, vol_sqrt_dt, N_steps, &rng);

        double payoff_ld = (double)payoff;
        payoff_sum += payoff_ld;
        payoff_sq_sum += payoff_ld * payoff_ld;

        /*
         * Weighted checksum reduces the chance that changes are hidden by
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
    result.payoff_sum = payoff_sum;
    result.payoff_sq_sum = payoff_sq_sum;
    result.checksum_payoff = checksum_payoff;

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

static double parse_double_arg(const char *s, const char *name, double min_value, int allow_zero) {
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);

    if (errno != 0 || end == s || *end != '\0' || !isfinite(v)) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }

    if (allow_zero) {
        if (v < min_value) {
            fprintf(stderr, "Invalid %s: %s\n", name, s);
            exit(EXIT_FAILURE);
        }
    } else {
        if (v <= min_value) {
            fprintf(stderr, "Invalid %s: %s\n", name, s);
            exit(EXIT_FAILURE);
        }
    }

    return v;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [S0] [K] [r] [sigma] [T] [N_steps] [N_paths] [iters] [seed]\n"
            "\n"
            "Defaults:\n"
            "  S0=100 K=100 r=0.05 sigma=0.20 T=1.0 N_steps=252 N_paths=200000 iters=3 seed=1\n"
            "\n"
            "Examples:\n"
            "  %s\n"
            "  %s 100 100 0.05 0.20 1.0 252 200000 3 1\n"
            "  %s 100 100 0.05 0.80 1.0 252 500000 3 1\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    double S0 = 100.0;
    double K = 100.0;
    double r = 0.05;
    double sigma = 0.20;
    double T = 1.0;
    int N_steps = 252;
    int N_paths = 200000;
    int iters = 3;
    uint64_t seed = 1;

    if (argc > 10) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) S0      = parse_double_arg(argv[1], "S0", 0.0, 0);
    if (argc >= 3) K       = parse_double_arg(argv[2], "K", 0.0, 1);
    if (argc >= 4) r       = parse_double_arg(argv[3], "r", -1.0e100, 1);
    if (argc >= 5) sigma   = parse_double_arg(argv[4], "sigma", 0.0, 1);
    if (argc >= 6) T       = parse_double_arg(argv[5], "T", 0.0, 0);
    if (argc >= 7) N_steps = parse_int_arg(argv[6], "N_steps", 1);
    if (argc >= 8) N_paths = parse_int_arg(argv[7], "N_paths", 1);
    if (argc >= 9) iters   = parse_int_arg(argv[8], "iters", 1);
    if (argc >= 10) seed   = parse_u64_arg(argv[9], "seed");

    AsianMCResult final_result;
    double total_time = 0.0;

    for (int it = 0; it < iters; ++it) {
        /*
         * Use a deterministic but different seed per iteration. The final
         * reported result is from the last iteration; timing is averaged.
         */
        uint64_t iter_seed = seed + (uint64_t)it * UINT64_C(0x9e3779b97f4a7c15);

        const double t0 = wall_time_seconds();
        final_result = price_asian_MC(S0, K, r, sigma, T, N_steps, N_paths, iter_seed);
        const double t1 = wall_time_seconds();

        total_time += (t1 - t0);
    }

    double avg_time = total_time / (double)iters;

    /*
     * Approximate FLOP count per path step:
     *   exponent_arg: vol_sqrt_dt*Z + drift          -> 2
     *   growth = exp(exponent_arg)                   -> counted as 1 expensive op here
     *   S *= growth                                  -> 1
     *   avg_sum += S                                 -> 1
     * Plus per path avg/payoff operations, ignored for simplicity.
     *
     * This is only an estimated throughput metric for relative comparison.
     */
    double ops_est = (5.0 * (double)N_steps + 8.0) * (double)N_paths;
    double gops_est = ops_est / avg_time / 1.0e9;

    printf("benchmark = asian_option_monte_carlo\n");
    printf("S0 = %.17g\n", S0);
    printf("K = %.17g\n", K);
    printf("r = %.17g\n", r);
    printf("sigma = %.17g\n", sigma);
    printf("T = %.17g\n", T);
    printf("N_steps = %d\n", N_steps);
    printf("N_paths = %d\n", N_paths);
    printf("iters = %d\n", iters);
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
    printf("payoff_sum = %.21e\n", final_result.payoff_sum);
    printf("payoff_sq_sum = %.21e\n", final_result.payoff_sq_sum);
    printf("checksum_payoff = %.21e\n", final_result.checksum_payoff);

    return EXIT_SUCCESS;
}
