// RUN: timeout -k 2 200 sh -c 'rm -rf "%t" && mkdir -p "%t" && cd "%t" && sh "%PERCI_TUNER/scripts/run.sh" search-optimize  "%s" -arg=20 -arg=10 -arg=1.0 -arg=100.0 -arg=2 -include-func=reductions -lm' > %t.out 2>&1
// RUN: ! grep -E 'fatal error:|: error:|error generated|undefined reference|undefined symbol|collect2:|^>>> Perci-Tuner Error' %t.out

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

/*
 * miniWeather serial C version
 * - Converted from the C++/MPI/PNetCDF miniWeather source supplied by the user.
 * - Removed all MPI and PNetCDF code.
 * - Removed all C++ features: references, constexpr, iostream, chrono.
 * - Keeps a single-file, libc + libm only build.
 * - Fixes two logic issues:
 *   1) DATA_SPEC_GRAVITY_WAVES source term was added once per NUM_VARS loop.
 *      It is now added only for ID_WMOM.
 *   2) hy_dens_cell / hy_dens_theta_cell vertical quadrature now uses qpoints.
 */

#ifndef _NX
#define _NX 400
#endif
#ifndef _NZ
#define _NZ 200
#endif
#ifndef _SIM_TIME
#define _SIM_TIME 100.0
#endif
#ifndef _OUT_FREQ
#define _OUT_FREQ -1.0
#endif
#ifndef _DATA_SPEC
#define _DATA_SPEC 2
#endif

static const double pi        = 3.14159265358979323846264338327;
static const double grav      = 9.8;
static const double cp        = 1004.0;
static const double cv        = 717.0;
static const double rd        = 287.0;
static const double p0        = 1.0e5;
static const double C0        = 27.5629410929725921310572974482;
static const double gamm      = 1.40027894002789400278940027894;
static const double xlen      = 2.0e4;
static const double zlen      = 1.0e4;
static const double hv_beta   = 0.05;
static const double cfl       = 1.50;
static const double max_speed = 450.0;

enum { hs = 2, sten_size = 4 };
enum { NUM_VARS = 4 };
enum { ID_DENS = 0, ID_UMOM = 1, ID_WMOM = 2, ID_RHOT = 3 };
enum { DIR_X = 1, DIR_Z = 2 };
enum {
    DATA_SPEC_COLLISION       = 1,
    DATA_SPEC_THERMAL         = 2,
    DATA_SPEC_GRAVITY_WAVES   = 3,
    DATA_SPEC_DENSITY_CURRENT = 5,
    DATA_SPEC_INJECTION       = 6
};

enum { nqpoints = 3 };
static const double qpoints[nqpoints] = {
    0.112701665379258311482073460022,
    0.500000000000000000000000000000,
    0.887298334620741688517926539980
};
static const double qweights[nqpoints] = {
    0.277777777777777777777777777779,
    0.444444444444444444444444444444,
    0.277777777777777777777777777779
};

/* Runtime-configurable values. Defaults come from -D macros above. */
static int    nx_glob       = _NX;
static int    nz_glob       = _NZ;
static double sim_time      = _SIM_TIME;
static double output_freq   = _OUT_FREQ;
static int    data_spec_int = _DATA_SPEC;
static double dx, dz;

/* Static over the course of the simulation. */
static double dt;
static int nx, nz;
static int i_beg, k_beg;
static int mainproc = 1;
static double *hy_dens_cell;
static double *hy_dens_theta_cell;
static double *hy_dens_int;
static double *hy_dens_theta_int;
static double *hy_pressure_int;

/* Dynamic over the course of the simulation. */
static double etime;
static double output_counter;
static double *state;
static double *state_tmp;
static double *flux;
static double *tend;
static int num_out = 0;
static int direction_switch = 1;
static double mass0, te0;
static double mass, te;

static inline double dmin2(double a, double b) { return (a < b) ? a : b; }

static void   init(int argc, char **argv);
static void   finalize(void);
static void   injection(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht);
static void   density_current(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht);
static void   gravity_waves(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht);
static void   thermal(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht);
static void   collision(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht);
static void   hydro_const_theta(double z, double *r, double *t);
static void   hydro_const_bvfreq(double z, double bv_freq0, double *r, double *t);
static double sample_ellipse_cosine(double x, double z, double amp, double x0, double z0, double xrad, double zrad);
static void   output_summary(double *state_in, double etime_in);
static void   perform_timestep(double *state_in, double *state_tmp_in, double *flux_in, double *tend_in, double dt_in);
static void   semi_discrete_step(double *state_init, double *state_forcing, double *state_out, double dt_in, int dir, double *flux_in, double *tend_in);
static void   compute_tendencies_x(double *state_in, double *flux_in, double *tend_in, double dt_in);
static void   compute_tendencies_z(double *state_in, double *flux_in, double *tend_in, double dt_in);
static void   set_halo_values_x(double *state_in);
static void   set_halo_values_z(double *state_in);
static void   reductions(double *mass_out, double *te_out);
static double wall_seconds(void);
static void   usage(const char *prog);
static void   validate_config(void);
static void  *xmalloc(size_t nbytes, const char *name);

static inline size_t state_idx(int ll, int k, int i) {
    return ((size_t)ll * (size_t)(nz + 2 * hs) * (size_t)(nx + 2 * hs)
            + (size_t)k * (size_t)(nx + 2 * hs) + (size_t)i);
}

static inline size_t flux_idx(int ll, int k, int i) {
    return ((size_t)ll * (size_t)(nz + 1) * (size_t)(nx + 1)
            + (size_t)k * (size_t)(nx + 1) + (size_t)i);
}

static inline size_t tend_idx(int ll, int k, int i) {
    return ((size_t)ll * (size_t)nz * (size_t)nx + (size_t)k * (size_t)nx + (size_t)i);
}

int main(int argc, char **argv) {
    init(argc, argv);

    reductions(&mass0, &te0);

    if (output_freq >= 0.0) {
        output_summary(state, etime);
    }

    double t1 = wall_seconds();
    while (etime < sim_time) {
        if (etime + dt > sim_time) {
            dt = sim_time - etime;
        }

        perform_timestep(state, state_tmp, flux, tend, dt);

#ifndef NO_INFORM
        if (mainproc) {
            printf("Elapsed Time: %.10f / %.10f\n", etime, sim_time);
        }
#endif

        etime += dt;
        output_counter += dt;

        if (output_freq >= 0.0 && output_counter >= output_freq) {
            output_counter -= output_freq;
            output_summary(state, etime);
        }
    }
    double t2 = wall_seconds();

    if (mainproc) {
        printf("CPU Time: %.6f sec\n", t2 - t1);
    }

    reductions(&mass, &te);

    if (mainproc) {
        printf("d_mass: %.16e\n", (mass - mass0) / mass0);
        printf("d_te:   %.16e\n", (te   - te0  ) / te0);
    }

    finalize();
    return 0;
}

static void perform_timestep(double *state_in, double *state_tmp_in, double *flux_in, double *tend_in, double dt_in) {
    if (direction_switch) {
        semi_discrete_step(state_in, state_in,      state_tmp_in, dt_in / 3.0, DIR_X, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_tmp_in, dt_in / 2.0, DIR_X, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_in,     dt_in / 1.0, DIR_X, flux_in, tend_in);

        semi_discrete_step(state_in, state_in,      state_tmp_in, dt_in / 3.0, DIR_Z, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_tmp_in, dt_in / 2.0, DIR_Z, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_in,     dt_in / 1.0, DIR_Z, flux_in, tend_in);
    } else {
        semi_discrete_step(state_in, state_in,      state_tmp_in, dt_in / 3.0, DIR_Z, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_tmp_in, dt_in / 2.0, DIR_Z, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_in,     dt_in / 1.0, DIR_Z, flux_in, tend_in);

        semi_discrete_step(state_in, state_in,      state_tmp_in, dt_in / 3.0, DIR_X, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_tmp_in, dt_in / 2.0, DIR_X, flux_in, tend_in);
        semi_discrete_step(state_in, state_tmp_in,  state_in,     dt_in / 1.0, DIR_X, flux_in, tend_in);
    }
    direction_switch = !direction_switch;
}

static void semi_discrete_step(double *state_init, double *state_forcing, double *state_out,
                               double dt_in, int dir, double *flux_in, double *tend_in) {
    if (dir == DIR_X) {
        set_halo_values_x(state_forcing);
        compute_tendencies_x(state_forcing, flux_in, tend_in, dt_in);
    } else if (dir == DIR_Z) {
        set_halo_values_z(state_forcing);
        compute_tendencies_z(state_forcing, flux_in, tend_in, dt_in);
    } else {
        fprintf(stderr, "Invalid direction: %d\n", dir);
        exit(EXIT_FAILURE);
    }

    for (int ll = 0; ll < NUM_VARS; ll++) {
        for (int k = 0; k < nz; k++) {
            for (int i = 0; i < nx; i++) {
                const size_t inds = state_idx(ll, k + hs, i + hs);
                const size_t indt = tend_idx(ll, k, i);

                /* Logic fix: the gravity-wave vertical perturbation belongs only to WMOM.
                 * The original code placed this block inside the ll loop without guarding by ll,
                 * so tend[ID_WMOM] was modified NUM_VARS times per cell. */
                if (data_spec_int == DATA_SPEC_GRAVITY_WAVES && ll == ID_WMOM) {
                    const double x = (i_beg + i + 0.5) * dx;
                    const double z = (k_beg + k + 0.5) * dz;
                    const double x0   = xlen / 8.0;
                    const double z0   = 1000.0;
                    const double xrad = 500.0;
                    const double zrad = 500.0;
                    const double amp  = 0.01;
                    const double dist = sqrt(((x - x0) / xrad) * ((x - x0) / xrad)
                                           + ((z - z0) / zrad) * ((z - z0) / zrad)) * pi / 2.0;
                    double wpert = 0.0;
                    if (dist <= pi / 2.0) {
                        const double c = cos(dist);
                        wpert = amp * c * c;
                    }
                    tend_in[indt] += wpert * hy_dens_cell[hs + k];
                }

                state_out[inds] = state_init[inds] + dt_in * tend_in[indt];
            }
        }
    }
}

static void compute_tendencies_x(double *state_in, double *flux_in, double *tend_in, double dt_in) {
    const double hv_coef = -hv_beta * dx / (16.0 * dt_in);

    for (int k = 0; k < nz; k++) {
        for (int i = 0; i < nx + 1; i++) {
            double d3_vals[NUM_VARS];
            double vals[NUM_VARS];

            for (int ll = 0; ll < NUM_VARS; ll++) {
                double stencil[sten_size];
                for (int s = 0; s < sten_size; s++) {
                    stencil[s] = state_in[state_idx(ll, k + hs, i + s)];
                }
                vals[ll]    = -stencil[0] / 12.0 + 7.0 * stencil[1] / 12.0 + 7.0 * stencil[2] / 12.0 - stencil[3] / 12.0;
                d3_vals[ll] = -stencil[0]        + 3.0 * stencil[1]        - 3.0 * stencil[2]        + stencil[3];
            }

            const double r = vals[ID_DENS] + hy_dens_cell[k + hs];
            const double u = vals[ID_UMOM] / r;
            const double w = vals[ID_WMOM] / r;
            const double t = (vals[ID_RHOT] + hy_dens_theta_cell[k + hs]) / r;
            const double p = C0 * pow((r * t), gamm);

            flux_in[flux_idx(ID_DENS, k, i)] = r * u       - hv_coef * d3_vals[ID_DENS];
            flux_in[flux_idx(ID_UMOM, k, i)] = r * u * u+p - hv_coef * d3_vals[ID_UMOM];
            flux_in[flux_idx(ID_WMOM, k, i)] = r * u * w   - hv_coef * d3_vals[ID_WMOM];
            flux_in[flux_idx(ID_RHOT, k, i)] = r * u * t   - hv_coef * d3_vals[ID_RHOT];
        }
    }

    for (int ll = 0; ll < NUM_VARS; ll++) {
        for (int k = 0; k < nz; k++) {
            for (int i = 0; i < nx; i++) {
                const size_t indt  = tend_idx(ll, k, i);
                const size_t indf1 = flux_idx(ll, k, i);
                const size_t indf2 = flux_idx(ll, k, i + 1);
                tend_in[indt] = -(flux_in[indf2] - flux_in[indf1]) / dx;
            }
        }
    }
}

static void compute_tendencies_z(double *state_in, double *flux_in, double *tend_in, double dt_in) {
    const double hv_coef = -hv_beta * dz / (16.0 * dt_in);

    for (int k = 0; k < nz + 1; k++) {
        for (int i = 0; i < nx; i++) {
            double d3_vals[NUM_VARS];
            double vals[NUM_VARS];

            for (int ll = 0; ll < NUM_VARS; ll++) {
                double stencil[sten_size];
                for (int s = 0; s < sten_size; s++) {
                    stencil[s] = state_in[state_idx(ll, k + s, i + hs)];
                }
                vals[ll]    = -stencil[0] / 12.0 + 7.0 * stencil[1] / 12.0 + 7.0 * stencil[2] / 12.0 - stencil[3] / 12.0;
                d3_vals[ll] = -stencil[0]        + 3.0 * stencil[1]        - 3.0 * stencil[2]        + stencil[3];
            }

            const double r = vals[ID_DENS] + hy_dens_int[k];
            const double u = vals[ID_UMOM] / r;
            double w       = vals[ID_WMOM] / r;
            const double t = (vals[ID_RHOT] + hy_dens_theta_int[k]) / r;
            const double p = C0 * pow((r * t), gamm) - hy_pressure_int[k];

            if (k == 0 || k == nz) {
                w = 0.0;
                d3_vals[ID_DENS] = 0.0;
            }

            flux_in[flux_idx(ID_DENS, k, i)] = r * w       - hv_coef * d3_vals[ID_DENS];
            flux_in[flux_idx(ID_UMOM, k, i)] = r * w * u   - hv_coef * d3_vals[ID_UMOM];
            flux_in[flux_idx(ID_WMOM, k, i)] = r * w * w+p - hv_coef * d3_vals[ID_WMOM];
            flux_in[flux_idx(ID_RHOT, k, i)] = r * w * t   - hv_coef * d3_vals[ID_RHOT];
        }
    }

    for (int ll = 0; ll < NUM_VARS; ll++) {
        for (int k = 0; k < nz; k++) {
            for (int i = 0; i < nx; i++) {
                const size_t indt  = tend_idx(ll, k, i);
                const size_t indf1 = flux_idx(ll, k,     i);
                const size_t indf2 = flux_idx(ll, k + 1, i);
                tend_in[indt] = -(flux_in[indf2] - flux_in[indf1]) / dz;
                if (ll == ID_WMOM) {
                    const size_t inds = state_idx(ID_DENS, k + hs, i + hs);
                    tend_in[indt] -= state_in[inds] * grav;
                }
            }
        }
    }
}

static void set_halo_values_x(double *state_in) {
    for (int ll = 0; ll < NUM_VARS; ll++) {
        for (int k = 0; k < nz; k++) {
            state_in[state_idx(ll, k + hs, 0)]           = state_in[state_idx(ll, k + hs, nx + hs - 2)];
            state_in[state_idx(ll, k + hs, 1)]           = state_in[state_idx(ll, k + hs, nx + hs - 1)];
            state_in[state_idx(ll, k + hs, nx + hs)]     = state_in[state_idx(ll, k + hs, hs)];
            state_in[state_idx(ll, k + hs, nx + hs + 1)] = state_in[state_idx(ll, k + hs, hs + 1)];
        }
    }

    if (data_spec_int == DATA_SPEC_INJECTION) {
        for (int k = 0; k < nz; k++) {
            for (int i = 0; i < hs; i++) {
                const double z = (k_beg + k + 0.5) * dz;
                if (fabs(z - 3.0 * zlen / 4.0) <= zlen / 16.0) {
                    const size_t ind_r = state_idx(ID_DENS, k + hs, i);
                    const size_t ind_u = state_idx(ID_UMOM, k + hs, i);
                    const size_t ind_t = state_idx(ID_RHOT, k + hs, i);
                    state_in[ind_u] = (state_in[ind_r] + hy_dens_cell[k + hs]) * 50.0;
                    state_in[ind_t] = (state_in[ind_r] + hy_dens_cell[k + hs]) * 298.0 - hy_dens_theta_cell[k + hs];
                }
            }
        }
    }
}

static void set_halo_values_z(double *state_in) {
    for (int ll = 0; ll < NUM_VARS; ll++) {
        for (int i = 0; i < nx + 2 * hs; i++) {
            if (ll == ID_WMOM) {
                state_in[state_idx(ll, 0,           i)] = 0.0;
                state_in[state_idx(ll, 1,           i)] = 0.0;
                state_in[state_idx(ll, nz + hs,     i)] = 0.0;
                state_in[state_idx(ll, nz + hs + 1, i)] = 0.0;
            } else if (ll == ID_UMOM) {
                state_in[state_idx(ll, 0,           i)] = state_in[state_idx(ll, hs,        i)] / hy_dens_cell[hs]        * hy_dens_cell[0];
                state_in[state_idx(ll, 1,           i)] = state_in[state_idx(ll, hs,        i)] / hy_dens_cell[hs]        * hy_dens_cell[1];
                state_in[state_idx(ll, nz + hs,     i)] = state_in[state_idx(ll, nz + hs - 1, i)] / hy_dens_cell[nz + hs - 1] * hy_dens_cell[nz + hs];
                state_in[state_idx(ll, nz + hs + 1, i)] = state_in[state_idx(ll, nz + hs - 1, i)] / hy_dens_cell[nz + hs - 1] * hy_dens_cell[nz + hs + 1];
            } else {
                state_in[state_idx(ll, 0,           i)] = state_in[state_idx(ll, hs,          i)];
                state_in[state_idx(ll, 1,           i)] = state_in[state_idx(ll, hs,          i)];
                state_in[state_idx(ll, nz + hs,     i)] = state_in[state_idx(ll, nz + hs - 1, i)];
                state_in[state_idx(ll, nz + hs + 1, i)] = state_in[state_idx(ll, nz + hs - 1, i)];
            }
        }
    }
}

static void init(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    if (argc == 6) {
        nx_glob       = atoi(argv[1]);
        nz_glob       = atoi(argv[2]);
        sim_time      = atof(argv[3]);
        output_freq   = atof(argv[4]);
        data_spec_int = atoi(argv[5]);
    } else if (argc != 1) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    validate_config();

    dx = xlen / (double)nx_glob;
    dz = zlen / (double)nz_glob;

    i_beg = 0;
    k_beg = 0;
    nx = nx_glob;
    nz = nz_glob;

    const size_t n_state = (size_t)(nx + 2 * hs) * (size_t)(nz + 2 * hs) * (size_t)NUM_VARS;
    const size_t n_flux  = (size_t)(nx + 1) * (size_t)(nz + 1) * (size_t)NUM_VARS;
    const size_t n_tend  = (size_t)nx * (size_t)nz * (size_t)NUM_VARS;

    state              = (double *)xmalloc(n_state * sizeof(double), "state");
    state_tmp          = (double *)xmalloc(n_state * sizeof(double), "state_tmp");
    flux               = (double *)xmalloc(n_flux  * sizeof(double), "flux");
    tend               = (double *)xmalloc(n_tend  * sizeof(double), "tend");
    hy_dens_cell       = (double *)xmalloc((size_t)(nz + 2 * hs) * sizeof(double), "hy_dens_cell");
    hy_dens_theta_cell = (double *)xmalloc((size_t)(nz + 2 * hs) * sizeof(double), "hy_dens_theta_cell");
    hy_dens_int        = (double *)xmalloc((size_t)(nz + 1) * sizeof(double), "hy_dens_int");
    hy_dens_theta_int  = (double *)xmalloc((size_t)(nz + 1) * sizeof(double), "hy_dens_theta_int");
    hy_pressure_int    = (double *)xmalloc((size_t)(nz + 1) * sizeof(double), "hy_pressure_int");

    dt = dmin2(dx, dz) / max_speed * cfl;
    etime = 0.0;
    output_counter = 0.0;

    if (mainproc) {
        printf("nx_glob, nz_glob: %d %d\n", nx_glob, nz_glob);
        printf("dx,dz: %.10f %.10f\n", dx, dz);
        printf("dt: %.10f\n", dt);
        printf("data_spec: %d\n", data_spec_int);
    }

    for (int k = 0; k < nz + 2 * hs; k++) {
        for (int i = 0; i < nx + 2 * hs; i++) {
            for (int ll = 0; ll < NUM_VARS; ll++) {
                state[state_idx(ll, k, i)] = 0.0;
            }

            for (int kk = 0; kk < nqpoints; kk++) {
                for (int ii = 0; ii < nqpoints; ii++) {
                    const double x = (i_beg + i - hs + 0.5) * dx + (qpoints[ii] - 0.5) * dx;
                    const double z = (k_beg + k - hs + 0.5) * dz + (qpoints[kk] - 0.5) * dz;
                    double r = 0.0, u = 0.0, w = 0.0, t = 0.0, hr = 0.0, ht = 0.0;

                    if      (data_spec_int == DATA_SPEC_COLLISION)       collision(x, z, &r, &u, &w, &t, &hr, &ht);
                    else if (data_spec_int == DATA_SPEC_THERMAL)         thermal(x, z, &r, &u, &w, &t, &hr, &ht);
                    else if (data_spec_int == DATA_SPEC_GRAVITY_WAVES)   gravity_waves(x, z, &r, &u, &w, &t, &hr, &ht);
                    else if (data_spec_int == DATA_SPEC_DENSITY_CURRENT) density_current(x, z, &r, &u, &w, &t, &hr, &ht);
                    else if (data_spec_int == DATA_SPEC_INJECTION)       injection(x, z, &r, &u, &w, &t, &hr, &ht);
                    else {
                        fprintf(stderr, "Invalid data_spec_int: %d\n", data_spec_int);
                        exit(EXIT_FAILURE);
                    }

                    const double weight = qweights[ii] * qweights[kk];
                    state[state_idx(ID_DENS, k, i)] += r * weight;
                    state[state_idx(ID_UMOM, k, i)] += (r + hr) * u * weight;
                    state[state_idx(ID_WMOM, k, i)] += (r + hr) * w * weight;
                    state[state_idx(ID_RHOT, k, i)] += ((r + hr) * (t + ht) - hr * ht) * weight;
                }
            }

            for (int ll = 0; ll < NUM_VARS; ll++) {
                state_tmp[state_idx(ll, k, i)] = state[state_idx(ll, k, i)];
            }
        }
    }

    for (int k = 0; k < nz + 2 * hs; k++) {
        hy_dens_cell[k] = 0.0;
        hy_dens_theta_cell[k] = 0.0;
        for (int kk = 0; kk < nqpoints; kk++) {
            /* Logic fix: use quadrature points in the vertical cell average. */
            const double z = (k_beg + k - hs + 0.5) * dz + (qpoints[kk] - 0.5) * dz;
            double r = 0.0, u = 0.0, w = 0.0, t = 0.0, hr = 0.0, ht = 0.0;

            if      (data_spec_int == DATA_SPEC_COLLISION)       collision(0.0, z, &r, &u, &w, &t, &hr, &ht);
            else if (data_spec_int == DATA_SPEC_THERMAL)         thermal(0.0, z, &r, &u, &w, &t, &hr, &ht);
            else if (data_spec_int == DATA_SPEC_GRAVITY_WAVES)   gravity_waves(0.0, z, &r, &u, &w, &t, &hr, &ht);
            else if (data_spec_int == DATA_SPEC_DENSITY_CURRENT) density_current(0.0, z, &r, &u, &w, &t, &hr, &ht);
            else if (data_spec_int == DATA_SPEC_INJECTION)       injection(0.0, z, &r, &u, &w, &t, &hr, &ht);

            hy_dens_cell[k]       += hr      * qweights[kk];
            hy_dens_theta_cell[k] += hr * ht * qweights[kk];
        }
    }

    for (int k = 0; k < nz + 1; k++) {
        const double z = (k_beg + k) * dz;
        double r = 0.0, u = 0.0, w = 0.0, t = 0.0, hr = 0.0, ht = 0.0;

        if      (data_spec_int == DATA_SPEC_COLLISION)       collision(0.0, z, &r, &u, &w, &t, &hr, &ht);
        else if (data_spec_int == DATA_SPEC_THERMAL)         thermal(0.0, z, &r, &u, &w, &t, &hr, &ht);
        else if (data_spec_int == DATA_SPEC_GRAVITY_WAVES)   gravity_waves(0.0, z, &r, &u, &w, &t, &hr, &ht);
        else if (data_spec_int == DATA_SPEC_DENSITY_CURRENT) density_current(0.0, z, &r, &u, &w, &t, &hr, &ht);
        else if (data_spec_int == DATA_SPEC_INJECTION)       injection(0.0, z, &r, &u, &w, &t, &hr, &ht);

        hy_dens_int[k] = hr;
        hy_dens_theta_int[k] = hr * ht;
        hy_pressure_int[k] = C0 * pow((hr * ht), gamm);
    }
}

static void injection(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht) {
    (void)x;
    hydro_const_theta(z, hr, ht);
    *r = 0.0;
    *t = 0.0;
    *u = 0.0;
    *w = 0.0;
}

static void density_current(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht) {
    hydro_const_theta(z, hr, ht);
    *r = 0.0;
    *t = sample_ellipse_cosine(x, z, -20.0, xlen / 2.0, 5000.0, 4000.0, 2000.0);
    *u = 0.0;
    *w = 0.0;
}

static void gravity_waves(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht) {
    (void)x;
    hydro_const_bvfreq(z, 0.02, hr, ht);
    *r = 0.0;
    *t = 0.0;
    *u = 15.0;
    *w = 0.0;
}

static void thermal(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht) {
    hydro_const_theta(z, hr, ht);
    *r = 0.0;
    *t = sample_ellipse_cosine(x, z, 3.0, xlen / 2.0, 2000.0, 2000.0, 2000.0);
    *u = 0.0;
    *w = 0.0;
}

static void collision(double x, double z, double *r, double *u, double *w, double *t, double *hr, double *ht) {
    hydro_const_theta(z, hr, ht);
    *r = 0.0;
    *t = sample_ellipse_cosine(x, z,  20.0, xlen / 2.0, 2000.0, 2000.0, 2000.0)
       + sample_ellipse_cosine(x, z, -20.0, xlen / 2.0, 8000.0, 2000.0, 2000.0);
    *u = 0.0;
    *w = 0.0;
}

static void hydro_const_theta(double z, double *r, double *t) {
    const double theta0 = 300.0;
    const double exner0 = 1.0;
    *t = theta0;
    const double exner = exner0 - grav * z / (cp * theta0);
    const double p = p0 * pow(exner, (cp / rd));
    const double rt = pow((p / C0), (1.0 / gamm));
    *r = rt / (*t);
}

static void hydro_const_bvfreq(double z, double bv_freq0, double *r, double *t) {
    const double theta0 = 300.0;
    const double exner0 = 1.0;
    *t = theta0 * exp(bv_freq0 * bv_freq0 / grav * z);
    const double exner = exner0 - grav * grav / (cp * bv_freq0 * bv_freq0) * ((*t) - theta0) / ((*t) * theta0);
    const double p = p0 * pow(exner, (cp / rd));
    const double rt = pow((p / C0), (1.0 / gamm));
    *r = rt / (*t);
}

static double sample_ellipse_cosine(double x, double z, double amp, double x0, double z0, double xrad, double zrad) {
    const double dist = sqrt(((x - x0) / xrad) * ((x - x0) / xrad)
                           + ((z - z0) / zrad) * ((z - z0) / zrad)) * pi / 2.0;
    if (dist <= pi / 2.0) {
        const double c = cos(dist);
        return amp * c * c;
    }
    return 0.0;
}

static void output_summary(double *state_in, double etime_in) {
    double min_dens =  1.0e300;
    double max_dens = -1.0e300;
    double min_theta =  1.0e300;
    double max_theta = -1.0e300;

    for (int k = 0; k < nz; k++) {
        for (int i = 0; i < nx; i++) {
            const double dens = state_in[state_idx(ID_DENS, k + hs, i + hs)];
            const double theta = (state_in[state_idx(ID_RHOT, k + hs, i + hs)] + hy_dens_theta_cell[k + hs])
                               / (hy_dens_cell[k + hs] + dens)
                               - hy_dens_theta_cell[k + hs] / hy_dens_cell[k + hs];
            if (dens < min_dens) min_dens = dens;
            if (dens > max_dens) max_dens = dens;
            if (theta < min_theta) min_theta = theta;
            if (theta > max_theta) max_theta = theta;
        }
    }

    printf("*** OUTPUT SUMMARY %d *** etime=%.10f dens=[%.6e, %.6e] theta=[%.6e, %.6e]\n",
           num_out, etime_in, min_dens, max_dens, min_theta, max_theta);
    num_out++;
}

static void reductions(double *mass_out, double *te_out) {
    double local_mass = 0.0;
    double local_te = 0.0;

    for (int k = 0; k < nz; k++) {
        for (int i = 0; i < nx; i++) {
            const double r  = state[state_idx(ID_DENS, k + hs, i + hs)] + hy_dens_cell[hs + k];
            const double u  = state[state_idx(ID_UMOM, k + hs, i + hs)] / r;
            const double w  = state[state_idx(ID_WMOM, k + hs, i + hs)] / r;
            const double th = (state[state_idx(ID_RHOT, k + hs, i + hs)] + hy_dens_theta_cell[hs + k]) / r;
            const double p  = C0 * pow(r * th, gamm);
            const double temp = th / pow(p0 / p, rd / cp);
            const double ke = r * (u * u + w * w);
            const double ie = r * cv * temp;
            local_mass += r * dx * dz;
            local_te   += (ke + ie) * dx * dz;
        }
    }

    *mass_out = local_mass;
    *te_out = local_te;
}

static void finalize(void) {
    free(state);
    free(state_tmp);
    free(flux);
    free(tend);
    free(hy_dens_cell);
    free(hy_dens_theta_cell);
    free(hy_dens_int);
    free(hy_dens_theta_int);
    free(hy_pressure_int);
}

static void validate_config(void) {
    if (nx_glob <= 0 || nz_glob <= 0) {
        fprintf(stderr, "Invalid grid size: nx=%d nz=%d\n", nx_glob, nz_glob);
        exit(EXIT_FAILURE);
    }
    if (sim_time < 0.0) {
        fprintf(stderr, "Invalid sim_time: %.17g\n", sim_time);
        exit(EXIT_FAILURE);
    }
    if (!(data_spec_int == DATA_SPEC_COLLISION ||
          data_spec_int == DATA_SPEC_THERMAL ||
          data_spec_int == DATA_SPEC_GRAVITY_WAVES ||
          data_spec_int == DATA_SPEC_DENSITY_CURRENT ||
          data_spec_int == DATA_SPEC_INJECTION)) {
        fprintf(stderr, "Invalid data_spec: %d. Valid values are 1,2,3,5,6.\n", data_spec_int);
        exit(EXIT_FAILURE);
    }
}

static void *xmalloc(size_t nbytes, const char *name) {
    void *p = malloc(nbytes);
    if (!p) {
        fprintf(stderr, "malloc failed for %s: %zu bytes\n", name, nbytes);
        exit(EXIT_FAILURE);
    }
    return p;
}

static double wall_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
    }
#endif
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s\n"
            "  %s NX NZ SIM_TIME OUT_FREQ DATA_SPEC\n\n"
            "Defaults can also be set at compile time:\n"
            "  cc -O3 -D_NX=400 -D_NZ=200 -D_SIM_TIME=100.0 -D_OUT_FREQ=-1.0 -D_DATA_SPEC=2 %s -lm -o miniweather_serial\n\n"
            "DATA_SPEC values:\n"
            "  1 collision\n"
            "  2 thermal\n"
            "  3 gravity_waves\n"
            "  5 density_current\n"
            "  6 injection\n",
            prog, prog, prog);
}
