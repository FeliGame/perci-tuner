// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/*
 * heston_mc_target.c
 *
 * Heston stochastic volatility Monte Carlo benchmark for mixed-precision tools.
 *
 * Mathematical structure:
 *
 *   dS = r*S*dt + sqrt(V)*S*dW_S
 *   dV = kappa*(theta - V)*dt + xi*sqrt(V)*dW_V
 *
 * with corr(dW_S, dW_V) = rho.
 *
 * Discrete Euler/log-Euler path simulation:
 *
 *   V_new = V + kappa*(theta - V)*dt
 *             + xi * sqrt(max(V,0)) * sqrt(dt) * Z_V
 *
 * Negative-variance handling schemes:
 *   scheme = 0: truncation       V_new = max(V_new, 0)
 *   scheme = 1: reflection       V_new = fabs(V_new)
 *   scheme = 2: full truncation  use max(V,0) in drift and diffusion,
 *                                then V_new = max(V_new,0)
 *
 * Asset update:
 *   S_new = S * exp((r - 0.5*max(V,0))*dt + sqrt(max(V,0))*sqrt(dt)*Z_S)
 *
 * European call payoff:
 *   payoff = max(S_T - K, 0)
 *
 * Purpose:
 *   This program exposes classic mixed-precision issues in stochastic
 *   volatility Monte Carlo simulation:
 *     - recursive stochastic state variables S and V
 *     - sqrt(max(V,0)) near zero
 *     - negative variance correction branch
 *     - exp() multiplicative asset update
 *     - correlated Gaussian generation
 *     - payoff cancellation near S_T - K
 *     - large-path payoff reduction
 *
 * Intended precision-search targets:
 *   - rng_uniform01()
 *   - gaussian_random()
 *   - heston_variance_step()
 *   - simulate_heston_payoff()
 *   - price_heston_MC()
 *
 * This file intentionally does not include a high-precision reference or full
 * error metrics. Use an external harness to compare the original baseline and
 * the mixed-precision transformed version.
 *
 * Build:
 *   gcc   -std=c99 -O3 -fno-fast-math -ffp-contract=off heston_mc_target.c -lm -o heston_mc
 *   clang -std=c99 -O3 -fno-fast-math -ffp-contract=off heston_mc_target.c -lm -o heston_mc
 *
 * Run:
 *   ./heston_mc [S0] [K] [r] [V0] [kappa] [theta] [xi] [rho] [T] [N_steps] [N_paths] [iters] [scheme] [seed]
 *
 * Examples:
 *   ./heston_mc
 *   ./heston_mc 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 252 200000 3 0 1
 *   ./heston_mc 100 100 0.03 0.04 1.0 0.04 1.0 -0.7 1.0 252 500000 3 1 1
 *
 * Notes:
 *   - The reported stderr_price is Monte Carlo sampling error, not numerical error.
 *   - Keep seed, N_steps, N_paths, and scheme fixed for mixed-precision comparisons.
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
    double final_V;
    int negative_variance_proposals;
} HestonPathResult;

typedef struct {
    double price;
    double mean_payoff;
    double stderr_price;
    double payoff_variance;
    double mean_final_S;
    double mean_final_V;
    double payoff_sum;
    double payoff_sq_sum;
    double checksum_payoff;
    double final_S_sum;
    double final_V_sum;
    long long negative_variance_proposals;
} HestonMCResult;

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
 * One Euler step for Heston variance.
 *
 * scheme:
 *   0 = truncation
 *   1 = reflection
 *   2 = full truncation
 *
 * Precision-sensitive variables:
 *   - V
 *   - V_pos
 *   - sqrt_V
 *   - drift
 *   - diffusion
 *   - V_proposal
 *   - branch handling of negative V_proposal
 */
double heston_variance_step(double V,
                            double kappa,
                            double theta,
                            double xi,
                            double dt,
                            double sqrt_dt,
                            double Z_V,
                            int scheme,
                            int *negative_proposal) {
    double V_pos = fmax(V, 0.0);
    double sqrt_V = sqrt(V_pos);

    double drift;
    if (scheme == 2) {
        /* Full truncation uses max(V,0) in the drift as well. */
        drift = kappa * (theta - V_pos) * dt;
    } else {
        /* Matches the user-provided mathematical structure. */
        drift = kappa * (theta - V) * dt;
    }

    double diffusion = xi * sqrt_V * sqrt_dt * Z_V;
    double V_proposal = V + drift + diffusion;

    if (V_proposal < 0.0) {
        *negative_proposal += 1;
    }

    double V_new;
    if (scheme == 0 || scheme == 2) {
        V_new = fmax(V_proposal, 0.0);
    } else if (scheme == 1) {
        V_new = fabs(V_proposal);
    } else {
        fprintf(stderr, "Unsupported scheme=%d\n", scheme);
        exit(EXIT_FAILURE);
    }

    return V_new;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 4
 * ============================================================
 *
 * Simulate one Heston path and return a European call payoff.
 *
 * Precision-sensitive variables:
 *   - correlated normals Z_S and Z_V
 *   - V and V_for_asset
 *   - asset exponent_arg
 *   - S multiplicative update
 *   - intrinsic = S - K
 */
HestonPathResult simulate_heston_payoff(double S0,
                                        double K,
                                        double r,
                                        double V0,
                                        double kappa,
                                        double theta,
                                        double xi,
                                        double rho,
                                        double dt,
                                        double sqrt_dt,
                                        int N_steps,
                                        int scheme,
                                        RNGState *rng) {
    HestonPathResult path;
    double S = S0;
    double V = V0;
    int neg_count = 0;

    double one_minus_rho2 = fmax(1.0 - rho * rho, 0.0);
    double sqrt_one_minus_rho2 = sqrt(one_minus_rho2);

    for (int t = 0; t < N_steps; ++t) {
        double Z_V = gaussian_random(rng);
        double Z_independent = gaussian_random(rng);
        double Z_S = rho * Z_V + sqrt_one_minus_rho2 * Z_independent;

        double V_for_asset = fmax(V, 0.0);
        double sqrt_V_for_asset = sqrt(V_for_asset);

        double exponent_arg = (r - 0.5 * V_for_asset) * dt
                            + sqrt_V_for_asset * sqrt_dt * Z_S;
        double growth = exp(exponent_arg);

        S *= growth;

        V = heston_variance_step(V, kappa, theta, xi, dt, sqrt_dt,
                                 Z_V, scheme, &neg_count);
    }

    double intrinsic = S - K;
    double payoff = fmax(intrinsic, 0.0);

    path.payoff = payoff;
    path.final_S = S;
    path.final_V = V;
    path.negative_variance_proposals = neg_count;

    return path;
}

/*
 * ============================================================
 * PRECISION-SEARCH TARGET 5
 * ============================================================
 *
 * Monte Carlo Heston European call pricing.
 *
 * Precision-sensitive variables:
 *   - discount
 *   - payoff_sum
 *   - payoff_sq_sum
 *   - final_S_sum / final_V_sum
 *   - variance estimate
 */
HestonMCResult price_heston_MC(double S0,
                               double K,
                               double r,
                               double V0,
                               double kappa,
                               double theta,
                               double xi,
                               double rho,
                               double T,
                               int N_steps,
                               int N_paths,
                               int scheme,
                               uint64_t seed) {
    HestonMCResult result;

    double dt = T / (double)N_steps;
    double sqrt_dt = sqrt(dt);
    double discount = exp(-r * T);

    double payoff_sum = 0.0;
    double payoff_sq_sum = 0.0;
    double checksum_payoff = 0.0;
    double final_S_sum = 0.0;
    double final_V_sum = 0.0;
    long long negative_variance_proposals = 0;

    RNGState rng;
    rng.state = seed + UINT64_C(0x13198a2e03707344);
    rng.has_spare = 0;
    rng.spare = 0.0;

    for (int p = 0; p < N_paths; ++p) {
        HestonPathResult path = simulate_heston_payoff(S0, K, r, V0,
                                                       kappa, theta, xi, rho,
                                                       dt, sqrt_dt, N_steps,
                                                       scheme, &rng);

        double payoff_ld = (double)path.payoff;
        payoff_sum += payoff_ld;
        payoff_sq_sum += payoff_ld * payoff_ld;
        final_S_sum += (double)path.final_S;
        final_V_sum += (double)path.final_V;
        negative_variance_proposals += (long long)path.negative_variance_proposals;

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
    result.mean_final_S = (double)(final_S_sum / (double)N_paths);
    result.mean_final_V = (double)(final_V_sum / (double)N_paths);
    result.payoff_sum = payoff_sum;
    result.payoff_sq_sum = payoff_sq_sum;
    result.checksum_payoff = checksum_payoff;
    result.final_S_sum = final_S_sum;
    result.final_V_sum = final_V_sum;
    result.negative_variance_proposals = negative_variance_proposals;

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
            "Usage: %s [S0] [K] [r] [V0] [kappa] [theta] [xi] [rho] [T] [N_steps] [N_paths] [iters] [scheme] [seed]\n"
            "\n"
            "Defaults:\n"
            "  S0=100 K=100 r=0.03 V0=0.04 kappa=2.0 theta=0.04 xi=0.5 rho=-0.7 T=1.0\n"
            "  N_steps=252 N_paths=200000 iters=3 scheme=0 seed=1\n"
            "\n"
            "scheme:\n"
            "  0: truncation       V_new = max(V_new, 0)\n"
            "  1: reflection       V_new = fabs(V_new)\n"
            "  2: full truncation  drift uses max(V,0), then V_new = max(V_new,0)\n"
            "\n"
            "Examples:\n"
            "  %s\n"
            "  %s 100 100 0.03 0.04 2.0 0.04 0.5 -0.7 1.0 252 200000 3 0 1\n"
            "  %s 100 100 0.03 0.04 1.0 0.04 1.0 -0.7 1.0 252 500000 3 1 1\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    double S0 = 100.0;
    double K = 100.0;
    double r = 0.03;
    double V0 = 0.04;
    double kappa = 2.0;
    double theta = 0.04;
    double xi = 0.50;
    double rho = -0.70;
    double T = 1.0;
    int N_steps = 252;
    int N_paths = 200000;
    int iters = 3;
    int scheme = 0;
    uint64_t seed = 1;

    if (argc > 15) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2)  S0      = parse_double_arg(argv[1],  "S0",    0.0, 1);
    if (argc >= 3)  K       = parse_double_arg(argv[2],  "K",     0.0, 0);
    if (argc >= 4)  r       = parse_double_arg(argv[3],  "r",    -1.0e100, 0);
    if (argc >= 5)  V0      = parse_double_arg(argv[4],  "V0",    0.0, 0);
    if (argc >= 6)  kappa   = parse_double_arg(argv[5],  "kappa", 0.0, 1);
    if (argc >= 7)  theta   = parse_double_arg(argv[6],  "theta", 0.0, 0);
    if (argc >= 8)  xi      = parse_double_arg(argv[7],  "xi",    0.0, 0);
    if (argc >= 9)  rho     = parse_double_arg(argv[8],  "rho",  -1.0, 0);
    if (argc >= 10) T       = parse_double_arg(argv[9],  "T",     0.0, 1);
    if (argc >= 11) N_steps = parse_int_arg(argv[10], "N_steps", 1);
    if (argc >= 12) N_paths = parse_int_arg(argv[11], "N_paths", 1);
    if (argc >= 13) iters   = parse_int_arg(argv[12], "iters",   1);
    if (argc >= 14) scheme  = parse_int_arg(argv[13], "scheme",  0);
    if (argc >= 15) seed    = parse_u64_arg(argv[14], "seed");

    if (rho < -1.0 || rho > 1.0) {
        fprintf(stderr, "rho must be in [-1, 1]\n");
        return EXIT_FAILURE;
    }

    if (scheme < 0 || scheme > 2) {
        fprintf(stderr, "scheme must be 0, 1, or 2\n");
        return EXIT_FAILURE;
    }

    HestonMCResult final_result;
    double total_time = 0.0;

    for (int it = 0; it < iters; ++it) {
        uint64_t iter_seed = seed + (uint64_t)it * UINT64_C(0x9e3779b97f4a7c15);

        const double t0 = wall_time_seconds();
        final_result = price_heston_MC(S0, K, r, V0, kappa, theta, xi, rho,
                                       T, N_steps, N_paths, scheme, iter_seed);
        const double t1 = wall_time_seconds();

        total_time += (t1 - t0);
    }

    double avg_time = total_time / (double)iters;

    /*
     * Approximate operations per path step:
     *   two Gaussian samples via Box-Muller cache, correlation, exp update,
     *   variance step with sqrt/fmax branch.
     * This is only a relative throughput estimate.
     */
    double ops_est = (18.0 * (double)N_steps + 12.0) * (double)N_paths;
    double gops_est = ops_est / avg_time / 1.0e9;

    printf("benchmark = heston_stochastic_volatility_mc\n");
    printf("S0 = %.17g\n", S0);
    printf("K = %.17g\n", K);
    printf("r = %.17g\n", r);
    printf("V0 = %.17g\n", V0);
    printf("kappa = %.17g\n", kappa);
    printf("theta = %.17g\n", theta);
    printf("xi = %.17g\n", xi);
    printf("rho = %.17g\n", rho);
    printf("T = %.17g\n", T);
    printf("N_steps = %d\n", N_steps);
    printf("N_paths = %d\n", N_paths);
    printf("iters = %d\n", iters);
    printf("scheme = %d\n", scheme);
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
    printf("mean_final_S = %.17g\n", final_result.mean_final_S);
    printf("mean_final_V = %.17g\n", final_result.mean_final_V);
    printf("negative_variance_proposals = %lld\n", final_result.negative_variance_proposals);
    printf("payoff_sum = %.21e\n", final_result.payoff_sum);
    printf("payoff_sq_sum = %.21e\n", final_result.payoff_sq_sum);
    printf("final_S_sum = %.21e\n", final_result.final_S_sum);
    printf("final_V_sum = %.21e\n", final_result.final_V_sum);
    printf("checksum_payoff = %.21e\n", final_result.checksum_payoff);

    return EXIT_SUCCESS;
}
