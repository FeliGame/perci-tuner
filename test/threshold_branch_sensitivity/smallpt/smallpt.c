// RUN: timeout -k 2 10 sh -c 'cd "%PERCI_TUNER" && sh scripts/run.sh search-optimize  "%s" -lm' > %t.out 2>&1; true
// RUN: ! grep -q '^>>> Perci-Tuner Error' %t.out

/*
 * smallpt.c
 * Pure C version of Kevin Beason's smallpt-style path tracer.
 *
 * Changes from the uploaded C++ version:
 *   - Converted C++ classes/operators/constructors to pure C structs/functions.
 *   - Replaced extern "C" _start() with standard int main().
 *   - Initialized image buffer correctly with calloc().
 *   - Replaced erand48() with a small per-pixel uint64_t RNG for portability and
 *     deterministic OpenMP execution.
 *   - Guarded Russian roulette against zero reflectance.
 *   - Added basic argument parsing and allocation/file error checks.
 *   - Kept the original Cornell-box scene, materials, camera, and path-tracing logic.
 *
 * Compile:
 *   gcc -std=c99 -O3 -fopenmp smallpt.c -lm -o smallpt
 *   clang -std=c99 -O3 -fopenmp smallpt.c -lm -o smallpt
 *
 * Run:
 *   ./smallpt [spp] [width] [height] [output.ppm]
 *
 * Examples:
 *   ./smallpt 16 320 240 image.ppm
 *   ./smallpt 5000 1024 768 image.ppm
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif

#ifndef DEFAULT_W
#define DEFAULT_W 1024
#endif

#ifndef DEFAULT_H
#define DEFAULT_H 768
#endif

#ifndef DEFAULT_SPP
#define DEFAULT_SPP 5000
#endif

typedef struct {
    double x, y, z;
} Vec;

typedef struct {
    Vec o, d;
} Ray;

typedef enum {
    DIFF,
    SPEC,
    REFR
} Refl_t;

typedef struct {
    double rad;
    Vec p;
    Vec e;
    Vec c;
    Refl_t refl;
} Sphere;

static Sphere spheres[9];

static inline Vec vec_make(double x, double y, double z) {
    Vec v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

static inline Vec vec_add(Vec a, Vec b) {
    return vec_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec vec_sub(Vec a, Vec b) {
    return vec_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline Vec vec_scale(Vec a, double s) {
    return vec_make(a.x * s, a.y * s, a.z * s);
}

static inline Vec vec_mult(Vec a, Vec b) {
    return vec_make(a.x * b.x, a.y * b.y, a.z * b.z);
}

static inline double vec_dot(Vec a, Vec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec vec_cross(Vec a, Vec b) {
    return vec_make(a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x);
}

static inline Vec vec_norm(Vec a) {
    double len2 = vec_dot(a, a);
    if (len2 <= 0.0) {
        return vec_make(0.0, 0.0, 0.0);
    }
    return vec_scale(a, 1.0 / sqrt(len2));
}

static inline Ray ray_make(Vec o, Vec d) {
    Ray r;
    r.o = o;
    r.d = d;
    return r;
}

static inline Sphere sphere_make(double rad, Vec p, Vec e, Vec c, Refl_t refl) {
    Sphere s;
    s.rad = rad;
    s.p = p;
    s.e = e;
    s.c = c;
    s.refl = refl;
    return s;
}

static inline double clamp01(double x) {
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

static inline int to_int(double x) {
    return (int)(pow(clamp01(x), 1.0 / 2.2) * 255.0 + 0.5);
}

/* SplitMix64-style generator. Good enough for this benchmark-style renderer. */
static inline uint64_t rng_next_u64(uint64_t *state) {
    uint64_t z;
    *state += UINT64_C(0x9E3779B97F4A7C15);
    z = *state;
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

static inline double rng_uniform(uint64_t *state) {
    /* Convert top 53 random bits to [0,1). */
    return (double)(rng_next_u64(state) >> 11) * 0x1.0p-53;
}

static inline uint64_t rng_seed(int x, int y, int sx, int sy) {
    uint64_t s = UINT64_C(0x123456789ABCDEF0);
    s ^= (uint64_t)(x + 1) * UINT64_C(0x9E3779B185EBCA87);
    s ^= (uint64_t)(y + 1) * UINT64_C(0xC2B2AE3D27D4EB4F);
    s ^= (uint64_t)(sx + 1) * UINT64_C(0x165667B19E3779F9);
    s ^= (uint64_t)(sy + 1) * UINT64_C(0x85EBCA77C2B2AE63);
    return s;
}

static double sphere_intersect(const Sphere *s, const Ray *r) {
    Vec op = vec_sub(s->p, r->o);
    double eps = 1e-4;
    double b = vec_dot(op, r->d);
    double det = b * b - vec_dot(op, op) + s->rad * s->rad;
    double t;

    if (det < 0.0) {
        return 0.0;
    }

    det = sqrt(det);
    t = b - det;
    if (t > eps) {
        return t;
    }

    t = b + det;
    return t > eps ? t : 0.0;
}

static int scene_intersect(const Ray *r, double *t, int *id) {
    int i;
    double d;
    const double inf = 1e20;
    *t = inf;
    *id = -1;

    for (i = 0; i < (int)(sizeof(spheres) / sizeof(spheres[0])); ++i) {
        d = sphere_intersect(&spheres[i], r);
        if (d != 0.0 && d < *t) {
            *t = d;
            *id = i;
        }
    }

    return *t < inf;
}

static Vec radiance(const Ray *r, int depth, uint64_t *rng_state) {
    double t;
    int id;

    if (!scene_intersect(r, &t, &id)) {
        return vec_make(0.0, 0.0, 0.0);
    }

    const Sphere *obj = &spheres[id];
    Vec x = vec_add(r->o, vec_scale(r->d, t));
    Vec n = vec_norm(vec_sub(x, obj->p));
    Vec nl = vec_dot(n, r->d) < 0.0 ? n : vec_scale(n, -1.0);
    Vec f = obj->c;
    double p = f.x > f.y && f.x > f.z ? f.x : (f.y > f.z ? f.y : f.z);

    ++depth;

    /* Russian roulette path termination. Guard p <= 0 to avoid 1/0. */
    if (depth > 5) {
        if (p <= 0.0) {
            return obj->e;
        }
        if (rng_uniform(rng_state) < p) {
            f = vec_scale(f, 1.0 / p);
        } else {
            return obj->e;
        }
    }

    if (obj->refl == DIFF) {
        double r1 = 2.0 * M_PI * rng_uniform(rng_state);
        double r2 = rng_uniform(rng_state);
        double r2s = sqrt(r2);
        Vec w = nl;
        Vec u = vec_norm(vec_cross(fabs(w.x) > 0.1 ? vec_make(0.0, 1.0, 0.0)
                                                   : vec_make(1.0, 0.0, 0.0),
                                   w));
        Vec v = vec_cross(w, u);
        Vec d = vec_norm(vec_add(vec_add(vec_scale(u, cos(r1) * r2s),
                                       vec_scale(v, sin(r1) * r2s)),
                               vec_scale(w, sqrt(1.0 - r2))));
        Ray nr = ray_make(x, d);
        return vec_add(obj->e, vec_mult(f, radiance(&nr, depth, rng_state)));
    }

    if (obj->refl == SPEC) {
        Vec refl_dir = vec_sub(r->d, vec_scale(n, 2.0 * vec_dot(n, r->d)));
        Ray refl_ray = ray_make(x, refl_dir);
        return vec_add(obj->e, vec_mult(f, radiance(&refl_ray, depth, rng_state)));
    }

    /* Ideal dielectric refraction. */
    Vec refl_dir = vec_sub(r->d, vec_scale(n, 2.0 * vec_dot(n, r->d)));
    Ray refl_ray = ray_make(x, refl_dir);
    int into = vec_dot(n, nl) > 0.0;
    double nc = 1.0;
    double nt = 1.5;
    double nnt = into ? nc / nt : nt / nc;
    double ddn = vec_dot(r->d, nl);
    double cos2t = 1.0 - nnt * nnt * (1.0 - ddn * ddn);

    if (cos2t < 0.0) {
        return vec_add(obj->e, vec_mult(f, radiance(&refl_ray, depth, rng_state)));
    }

    Vec tdir = vec_norm(vec_sub(vec_scale(r->d, nnt),
                                vec_scale(n, (into ? 1.0 : -1.0) *
                                             (ddn * nnt + sqrt(cos2t)))));
    double a = nt - nc;
    double b = nt + nc;
    double R0 = a * a / (b * b);
    double c = 1.0 - (into ? -ddn : vec_dot(tdir, n));
    double Re = R0 + (1.0 - R0) * c * c * c * c * c;
    double Tr = 1.0 - Re;
    double P = 0.25 + 0.5 * Re;
    double RP = Re / P;
    double TP = Tr / (1.0 - P);
    Ray trans_ray = ray_make(x, tdir);

    if (depth > 2) {
        if (rng_uniform(rng_state) < P) {
            return vec_add(obj->e, vec_mult(f, vec_scale(radiance(&refl_ray, depth, rng_state), RP)));
        }
        return vec_add(obj->e, vec_mult(f, vec_scale(radiance(&trans_ray, depth, rng_state), TP)));
    }

    return vec_add(obj->e,
                   vec_mult(f,
                            vec_add(vec_scale(radiance(&refl_ray, depth, rng_state), Re),
                                    vec_scale(radiance(&trans_ray, depth, rng_state), Tr))));
}

static void init_scene(void) {
    spheres[0] = sphere_make(1e5,  vec_make( 1e5 + 1.0, 40.8, 81.6), vec_make(0,0,0), vec_make(.75,.25,.25), DIFF); /* Left */
    spheres[1] = sphere_make(1e5,  vec_make(-1e5 + 99., 40.8, 81.6), vec_make(0,0,0), vec_make(.25,.25,.75), DIFF); /* Right */
    spheres[2] = sphere_make(1e5,  vec_make(50., 40.8,  1e5),        vec_make(0,0,0), vec_make(.75,.75,.75), DIFF); /* Back */
    spheres[3] = sphere_make(1e5,  vec_make(50., 40.8, -1e5 + 170.), vec_make(0,0,0), vec_make(0,0,0),       DIFF); /* Front */
    spheres[4] = sphere_make(1e5,  vec_make(50.,  1e5, 81.6),        vec_make(0,0,0), vec_make(.75,.75,.75), DIFF); /* Bottom */
    spheres[5] = sphere_make(1e5,  vec_make(50., -1e5 + 81.6, 81.6), vec_make(0,0,0), vec_make(.75,.75,.75), DIFF); /* Top */
    spheres[6] = sphere_make(16.5, vec_make(27., 16.5, 47.),         vec_make(0,0,0), vec_scale(vec_make(1,1,1), .999), SPEC); /* Mirror */
    spheres[7] = sphere_make(16.5, vec_make(73., 16.5, 78.),         vec_make(0,0,0), vec_scale(vec_make(1,1,1), .999), REFR); /* Glass */
    spheres[8] = sphere_make(600., vec_make(50., 681.6 - .27, 81.6), vec_make(12,12,12), vec_make(0,0,0), DIFF);      /* Light */
}

static int parse_positive_int(const char *s, int fallback) {
    char *end = NULL;
    long v;
    if (!s) return fallback;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v <= 0 || v > 1000000L) {
        return fallback;
    }
    return (int)v;
}

int main(int argc, char **argv) {
    int spp = argc > 1 ? parse_positive_int(argv[1], DEFAULT_SPP) : DEFAULT_SPP;
    int w   = argc > 2 ? parse_positive_int(argv[2], DEFAULT_W)   : DEFAULT_W;
    int h   = argc > 3 ? parse_positive_int(argv[3], DEFAULT_H)   : DEFAULT_H;
    const char *out_name = argc > 4 ? argv[4] : "image.ppm";

    int samples_per_subpixel = spp / 4;
    int effective_spp;
    Vec *c;
    Ray cam;
    Vec cx, cy;
    FILE *f;
    int i;

    if (samples_per_subpixel < 1) {
        samples_per_subpixel = 1;
    }
    effective_spp = samples_per_subpixel * 4;

    init_scene();

    c = (Vec *)calloc((size_t)w * (size_t)h, sizeof(Vec));
    if (!c) {
        fprintf(stderr, "Error: failed to allocate image buffer for %dx%d\n", w, h);
        return 1;
    }

    cam = ray_make(vec_make(50.0, 52.0, 295.6), vec_norm(vec_make(0.0, -0.042612, -1.0)));
    cx = vec_make((double)w * 0.5135 / (double)h, 0.0, 0.0);
    cy = vec_scale(vec_norm(vec_cross(cx, cam.d)), 0.5135);

    fprintf(stderr, "smallpt: %dx%d, %d spp, output=%s\n", w, h, effective_spp, out_name);

#if defined(_OPENMP) && !defined(NO_OPENMP_RENDER)
#pragma omp parallel for schedule(dynamic, 1)
#endif
    for (int y = 0; y < h; ++y) {
#ifndef NO_PROGRESS
#if defined(_OPENMP) && !defined(NO_OPENMP_RENDER)
#pragma omp critical
#endif
        {
            fprintf(stderr, "\rRendering (%d spp) %5.2f%%", effective_spp,
                    100.0 * (double)y / (double)(h > 1 ? h - 1 : 1));
            fflush(stderr);
        }
#endif
        for (int x = 0; x < w; ++x) {
            int pixel_index = (h - y - 1) * w + x;
            for (int sy = 0; sy < 2; ++sy) {
                for (int sx = 0; sx < 2; ++sx) {
                    Vec r = vec_make(0.0, 0.0, 0.0);
                    uint64_t rng_state = rng_seed(x, y, sx, sy);

                    for (int s = 0; s < samples_per_subpixel; ++s) {
                        double r1 = 2.0 * rng_uniform(&rng_state);
                        double dx = r1 < 1.0 ? sqrt(r1) - 1.0 : 1.0 - sqrt(2.0 - r1);
                        double r2 = 2.0 * rng_uniform(&rng_state);
                        double dy = r2 < 1.0 ? sqrt(r2) - 1.0 : 1.0 - sqrt(2.0 - r2);
                        Vec d = vec_add(vec_add(vec_scale(cx, (((double)sx + 0.5 + dx) / 2.0 + (double)x) / (double)w - 0.5),
                                                vec_scale(cy, (((double)sy + 0.5 + dy) / 2.0 + (double)y) / (double)h - 0.5)),
                                        cam.d);
                        Ray primary = ray_make(vec_add(cam.o, vec_scale(d, 140.0)), vec_norm(d));
                        r = vec_add(r, vec_scale(radiance(&primary, 0, &rng_state), 1.0 / (double)samples_per_subpixel));
                    }

                    c[pixel_index] = vec_add(c[pixel_index],
                                             vec_scale(vec_make(clamp01(r.x), clamp01(r.y), clamp01(r.z)), 0.25));
                }
            }
        }
    }

#ifndef NO_PROGRESS
    fprintf(stderr, "\rRendering (%d spp) 100.00%%\n", effective_spp);
#endif

    /* Output mean linear luminance to stdout for oracle comparison.
     * PerciTuner captures the last line of stdout as the result value.
     * Mean luminance is deterministic (fixed-seed RNG) and sensitive
     * to floating-point precision changes in the path-tracing computation. */
    {
        double sum = 0.0;
        int n = w * h;
        for (i = 0; i < n; ++i) {
            sum += c[i].x + c[i].y + c[i].z;
        }
        printf("%.17g\n", sum / (3.0 * n));
    }

    f = fopen(out_name, "w");
    if (!f) {
        fprintf(stderr, "Error: failed to open output file: %s\n", out_name);
        free(c);
        return 1;
    }

    fprintf(f, "P3\n%d %d\n%d\n", w, h, 255);
    for (i = 0; i < w * h; ++i) {
        fprintf(f, "%d %d %d ", to_int(c[i].x), to_int(c[i].y), to_int(c[i].z));
        if ((i + 1) % w == 0) {
            fputc('\n', f);
        }
    }

    fclose(f);
    free(c);
    return 0;
}
