#ifndef INLINE_H
#define INLINE_H
#include <stdio.h>
#include <stdlib.h>
#include "dd_inline.h"

#define _QD_SPLITTER 134217729.0               // = 2^27 + 1
#define _QD_SPLIT_THRESH 6.69692879491417e+299 // = 2^996
#define n_inv_fact 15

const double inv_fact[n_inv_fact][2] = {
    {1.66666666666666657e-01, 9.25185853854297066e-18},
    {4.16666666666666644e-02, 2.31296463463574266e-18},
    {8.33333333333333322e-03, 1.15648231731787138e-19},
    {1.38888888888888894e-03, -5.30054395437357706e-20},
    {1.98412698412698413e-04, 1.72095582934207053e-22},
    {2.48015873015873016e-05, 2.15119478667758816e-23},
    {2.75573192239858925e-06, -1.85839327404647208e-22},
    {2.75573192239858883e-07, 2.37677146222502973e-23},
    {2.50521083854417202e-08, -1.44881407093591197e-24},
    {2.08767569878681002e-09, -1.20734505911325997e-25},
    {1.60590438368216133e-10, 1.25852945887520981e-26},
    {1.14707455977297245e-11, 2.06555127528307454e-28},
    {7.64716373181981641e-13, 7.03872877733453001e-30},
    {4.77947733238738525e-14, 4.39920548583408126e-31},
    {2.81145725434552060e-15, 1.65088427308614326e-31}};

static const dd_real _pi16 = {1.963495408493620697e-01, 7.654042494670957545e-18};

/* Table of sin(k * pi/16) and cos(k * pi/16). */
static const double sin_table[4][2] = {
    {1.950903220161282758e-01, -7.991079068461731263e-18},
    {3.826834323650897818e-01, -1.005077269646158761e-17},
    {5.555702330196021776e-01, 4.709410940561676821e-17},
    {7.071067811865475727e-01, -4.833646656726456726e-17}};

static const double cos_table[4][2] = {
    {9.807852804032304306e-01, 1.854693999782500573e-17},
    {9.238795325112867385e-01, 1.764504708433667706e-17},
    {8.314696123025452357e-01, 1.407385698472802389e-18},
    {7.071067811865475727e-01, -4.833646656726456726e-17}};

#define PRINTDD(name, x, y) printf("in %s x=%le, y=%le\n", name, x, y)

inline double quick_two_sum(double a, double b, double *err);
inline double quick_two_diff(double a, double b, double *err);
inline double two_sum(double a, double b, double *err);
inline double two_diff(double a, double b, double *err);
inline void split(double a, double *hi, double *lo);
inline double two_prod(double a, double b, double *err);
inline double two_sqr(double a, double *err);
inline dd_real ieee_add(const dd_real a, const dd_real b);
inline dd_real sloppy_add(const dd_real a, const dd_real b);
inline dd_real dd_init(const double cx);
inline dd_real dd_init2(const double cx1, const double cx2);
inline dd_real add_d_d(double a, double b);
inline dd_real add_dd_d(const dd_real a, double b);
inline dd_real add_dd_dd(const dd_real a, const dd_real b);
inline dd_real add_d_dd(double a, const dd_real b);
inline dd_real sub_d_d(double a, double b);
inline dd_real sub_dd_d(const dd_real a, double b);
inline dd_real sub_dd_dd(const dd_real a, const dd_real b);
inline dd_real sub_d_dd(double a, const dd_real b);
inline dd_real neg_dd(const dd_real a);
inline dd_real mul_d_d(double a, double b);
inline dd_real ldexp_dd_i(const dd_real a, int exp);
inline dd_real mul_pwr2_dd_d(const dd_real a, double b);
inline dd_real mul_dd_d(const dd_real a, double b);
inline dd_real mul_dd_dd(const dd_real a, const dd_real b);
inline dd_real mul_d_dd(double a, const dd_real b);
inline dd_real div_d_d(double a, double b);
inline dd_real div_dd_d(const dd_real a, double b);
inline dd_real div_dd_dd(const dd_real a, const dd_real b);
inline dd_real div_d_dd(double a, const dd_real b);
inline dd_real inv_dd(const dd_real a);
inline dd_real sqr_dd(const dd_real a);
inline dd_real sqr_d(double a);
inline bool is_zero(const dd_real a);
inline bool is_one(const dd_real a);
inline bool is_positive(const dd_real a);
inline bool is_negative(const dd_real a);
inline dd_real npwr_dd_i(dd_real a, int n);
inline bool eq_dd_d(const dd_real a, double b);
inline bool eq_dd_dd(const dd_real a, const dd_real b);
inline bool eq_d_dd(double a, const dd_real b);
inline bool gt_dd_d(const dd_real a, double b);
inline bool gt_dd_dd(const dd_real a, const dd_real b);
inline bool gt_d_dd(double a, const dd_real b);
inline bool lt_dd_d(const dd_real a, double b);
inline bool lt_dd_dd(const dd_real a, const dd_real b);
inline bool lt_d_dd(double a, const dd_real b);
inline bool ge_dd_d(const dd_real a, double b);
inline bool ge_dd_dd(const dd_real a, const dd_real b);
inline bool ge_d_dd(double a, const dd_real b);
inline bool le_dd_d(const dd_real a, double b);
inline bool le_dd_dd(const dd_real a, const dd_real b);
inline bool le_d_dd(double a, const dd_real b);
inline bool ne_dd_d(const dd_real a, double b);
inline bool ne_dd_dd(const dd_real a, const dd_real b);
inline bool ne_d_dd(double a, const dd_real b);
inline dd_real abs_dd(const dd_real a);
inline dd_real fabs_dd(const dd_real a);
inline double nint(double d);
inline dd_real nint_dd(const dd_real a);
inline dd_real floor_dd(const dd_real a);
inline dd_real ceil_dd(const dd_real a);
inline dd_real aint_dd(const dd_real a);
inline dd_real drem_dd_dd(const dd_real a, const dd_real b);
inline dd_real divrem_dd_dd(const dd_real a, const dd_real b, dd_real *r);
inline dd_real exp_dd(const dd_real a);
inline dd_real log_dd(const dd_real a);
inline dd_real log10_dd(const dd_real a);
inline dd_real pow_dd_i(const dd_real a, int n);
inline dd_real pow_dd_dd(const dd_real a, const dd_real b);
inline dd_real sqrt_dd(const dd_real a);
inline dd_real nroot_dd_i(const dd_real a, int n);
static dd_real sin_taylor(const dd_real a);
static dd_real cos_taylor(const dd_real a);
static void sincos_taylor(const dd_real a, dd_real *sin_a, dd_real *cos_a);
dd_real sin_dd(const dd_real a);
dd_real cos_dd(const dd_real a);
dd_real atan2_dd_dd(const dd_real y, const dd_real x);
dd_real tan_dd(const dd_real a);
dd_real asin_dd(const dd_real a);
dd_real acos_dd(const dd_real a);
dd_real atan_dd(const dd_real a);
dd_real sinh_dd(const dd_real a);
dd_real cosh_dd(const dd_real a);
void sincos_dd(const dd_real a, dd_real *sin_a, dd_real *cos_a);
dd_real tanh_dd(const dd_real a);
dd_real asinh_dd(const dd_real a);
dd_real acosh_dd(const dd_real a);
void sincosh_dd(const dd_real a, dd_real *s, dd_real *c);
dd_real atanh_dd(const dd_real a);

#endif
