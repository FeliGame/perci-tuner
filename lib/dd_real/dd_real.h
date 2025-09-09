#ifndef DD_REAL_H
#define DD_REAL_H
#include "dd_inline.h"

#define DP(s, t) //printf("In %s, %lf, %lf\n", s, t.dd.x[0], t.dd.x[1])
//OUT

dd_real dd_init(const double cx);
dd_real dd_init2(const double cx1, const double cx2);

dd_real add_d_d(double a, double b);
dd_real add_dd_d(const dd_real a, double b);
dd_real add_dd_dd(const dd_real a, const dd_real b);
dd_real add_d_dd(double a, const dd_real b);
dd_real sub_d_d(double a, double b);
dd_real sub_dd_d(const dd_real a, double b);
dd_real sub_dd_dd(const dd_real a, const dd_real b);
dd_real sub_d_dd(double a, const dd_real b);
dd_real neg_dd(const dd_real a);
dd_real mul_d_d(double a, double b);
dd_real mul_dd_d(const dd_real a, double b);
dd_real mul_dd_dd(const dd_real a, const dd_real b);
dd_real mul_d_dd(double a, const dd_real b);
dd_real div_d_d(double a, double b);
dd_real div_dd_d(const dd_real a, double b);
dd_real div_dd_dd(const dd_real a, const dd_real b);
dd_real div_d_dd(double a, const dd_real b);

dd_real inv_dd(dd_real a);

dd_real drem_dd_dd(const dd_real a, const dd_real b);
dd_real divrem_dd_dd(const dd_real a, const dd_real b, dd_real *r);

dd_real pow_dd_i(dd_real a, int n);
dd_real pow_dd_dd(dd_real a, dd_real b);
dd_real npwr_dd_i(dd_real a, int n);
dd_real sqr_dd(dd_real a);

dd_real sqrt_dd(dd_real a);
dd_real nroot_dd_i(dd_real a, int n);

bool eq_dd_d(const dd_real a, double b);
bool eq_d_dd(double a, const dd_real b);
bool eq_dd_dd(const dd_real a, const dd_real b);

bool le_dd_d(const dd_real a, double b);
bool le_d_dd(double a, const dd_real b);
bool le_dd_dd(const dd_real a, const dd_real b);

bool ge_dd_d(const dd_real a, double b);
bool ge_d_dd(double a, const dd_real b);
bool ge_dd_dd(const dd_real a, const dd_real b);

bool lt_dd_d(const dd_real a, double b);
bool lt_d_dd(double a, const dd_real b);
bool lt_dd_dd(const dd_real a, const dd_real b);

bool gt_dd_d(const dd_real a, double b);
bool gt_d_dd(double a, const dd_real b);
bool gt_dd_dd(const dd_real a, const dd_real b);

bool ne_dd_d(const dd_real a, double b);
bool ne_d_dd(double a, const dd_real b);
bool ne_dd_dd(const dd_real a, const dd_real b);

dd_real nint_dd(const dd_real a);
dd_real floor_dd(const dd_real a);
dd_real ceil_dd(const dd_real a);
dd_real aint_dd(const dd_real a);

dd_real exp_dd(dd_real a);
dd_real ldexp_dd_i(const dd_real a, int exp);
dd_real log_dd(dd_real a);
dd_real log10_dd(const dd_real a);

dd_real sin_dd(const dd_real a);
dd_real cos_dd(const dd_real a);
dd_real tan_dd(const dd_real a);
void sincos_dd(const dd_real a, dd_real *sin_a, dd_real *cos_a);

dd_real asin_dd(const dd_real a);
dd_real acos_dd(const dd_real a);
dd_real atan_dd(const dd_real a);
dd_real atan2_dd_dd(const dd_real y, const dd_real x);

dd_real sinh_dd(const dd_real a);
dd_real cosh_dd(const dd_real a);
dd_real tanh_dd(const dd_real a);
void sincosh_dd(const dd_real a, dd_real *s, dd_real *c);

dd_real asinh_dd(const dd_real a);
dd_real acosh_dd(const dd_real a);
dd_real atanh_dd(const dd_real a);

dd_real abs_dd(dd_real a);
dd_real fabs_dd(const dd_real a);

#endif
