#ifndef LDOUBLE_H
#define LDOUBLE_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "dd_real.h"
#include "bf16.h"
//#include <arm_neon.h>

//#ifndef __bf16
//#define __bf16 unsigned short
//#endif
#ifndef fp128
#define fp128 long double
#endif

void FP128ToDD0(fp128 a, fp128 *r);

void dd_printf(fp128 a);

fp128  DDToFP128(fp128 a);
double DDToFP64(fp128 a);
float  DDToFP32(fp128 a);
__fp16 DDToFP16(fp128 a);
__bf16 DDToBP16(fp128 a);

fp128 FP128ToDD(fp128 a);
fp128 FP64ToDD(double a);
fp128 FP32ToDD(float a);
fp128 FP16ToDD(__fp16 a);
fp128 BP16ToDD(__bf16 a);

uint32_t DDToU32(fp128 a);
uint64_t DDToU64(fp128 a);
int32_t DDToS32(fp128 a);
int64_t DDToS64(fp128 a);

fp128 U32ToDD(uint32_t a);
fp128 U64ToDD(uint64_t a);
fp128 S32ToDD(int32_t a);
fp128 S64ToDD(int64_t a);

fp128 fp128_add22(fp128 a, fp128 b);
fp128 fp128_sub22(fp128 a, fp128 b);
fp128 fp128_mul22(fp128 a, fp128 b);
fp128 fp128_div22(fp128 a, fp128 b);

fp128 fp128_muladd222(fp128 a, fp128 b, fp128 c);

fp128 fp128_neg(fp128 a);

bool fp128_eq(fp128 a, fp128 b);
bool fp128_le(fp128 a, fp128 b);
bool fp128_ge(fp128 a, fp128 b);
bool fp128_lt(fp128 a, fp128 b);
bool fp128_gt(fp128 a, fp128 b);
bool fp128_ne(fp128 a, fp128 b);

//fp128 fp128_inv(fp128); // nouse
int fp128_fpclassify(fp128); // math.h classify fp
int fp128_signbit(fp128); // math.h get fp sign
int fp128_isinf(fp128); // math.h
int fp128_isnan(fp128); // math.h
int fp128_finite(fp128); //math.h if not inf and nan
int fp128_iseqsig(fp128, fp128); //math.h eq
int fp128_issignaling(fp128); //math sNaN or qNaN

//fp128 fp128_npwr(fp128 a, int n); //not in math
//fp128 fp128_nroot(fp128 a, int n); //not in math

//fp128 fp128_nint(fp128 a); // not in math
fp128 fp128_floor(fp128 a);
fp128 fp128_ceil(fp128 a);
//fp128 fp128_aint(fp128 a); // not in math

void fp128_sincos(fp128 a, fp128 *sin_a, fp128 *cos_a);
fp128 fp128_atan2(fp128 y, fp128 x);

fp128 fp128_sinh(fp128 a);
fp128 fp128_cosh(fp128 a);
fp128 fp128_tanh(fp128 a);
void fp128_sincosh(fp128 a, fp128 *s, fp128 *c);

fp128 fp128_asinh(fp128 a);
fp128 fp128_acosh(fp128 a);
fp128 fp128_atanh(fp128 a);

fp128 fp128_exp(fp128 a);
fp128 fp128_expm1(fp128 a); // math.h compute "exp(a)-1"
fp128 fp128_frexp(fp128 a, int *e); //math.h compute "a = return * 2^e"
fp128 fp128_ldexp(fp128 a, int exp);
fp128 fp128_exp2(fp128 a); // math.h compute "2^a"

fp128 fp128_log(fp128 a);
fp128 fp128_log10(fp128 a);
fp128 fp128_log1p(fp128 a); // math.h compute "log(a+1)"
fp128 fp128_logb(fp128 a); // math.h compute "ceil(log2(|a|))"
fp128 fp128_log2(fp128 a); // math.h compute "log2(a)"

fp128 fp128_drem(fp128 a, fp128 b);
fp128 fp128_divrem(fp128 a, fp128 b, fp128 *r);
fp128 fp128_modf(fp128 a, fp128 *r); //math.h "modf(1.23) = 1.0 + 0.23"
fp128 fp128_fmod(fp128, fp128); // math.h "return = x - ceil(x/y)*y"

fp128 fp128_pow(fp128 a, fp128 b);
fp128 fp128_sqr(fp128 a);
fp128 fp128_sqrt(fp128 a);

fp128 fp128_sin(fp128 a);
fp128 fp128_cos(fp128 a);
fp128 fp128_tan(fp128 a);

fp128 fp128_asin(fp128 a);
fp128 fp128_acos(fp128 a);
fp128 fp128_atan(fp128 a);

fp128 fp128_fabs(fp128 a);

fp128 fp128_hypot(fp128, fp128); // math.h sqrt(x^2 + y^2) not inf
fp128 fp128_cbrt(fp128); // math.h powl(a, 1.0L/3.0L) more safe more accuracy
fp128 fp128_significand(fp128); // math.h return significand of fp, similar to frexp
fp128 fp128_copysign(fp128, fp128); // math.h "return = x && sign(y)"
fp128 fp128_nan(char *); // math.h create a nan
fp128 fp128_j0(fp128);
fp128 fp128_j1(fp128);
fp128 fp128_jn(int, fp128);
fp128 fp128_y0(fp128);
fp128 fp128_y1(fp128);
fp128 fp128_yn(int, fp128);
fp128 fp128_erf(fp128);
fp128 fp128_erfc(fp128);
fp128 fp128_lgamma(fp128);
fp128 fp128_tgamma(fp128);
fp128 fp128_gamma(fp128);
fp128 fp128_lgamma_r(fp128, int *);
fp128 fp128_rint(fp128); // math.h equal to fp128_nint
fp128 fp128_nextafter(fp128, fp128); // next fp128
fp128 fp128_nexttoward(fp128, fp128); // equal to fp128_nextafter
fp128 fp128_scalbn(fp128, int); // x * 2^n
int fp128_ilogb(fp128); // ceil(log2(|x|))
fp128 fp128_nearbyint(fp128); // similar to rint, but not errno
fp128 fp128_round(fp128); // rounding
fp128 fp128_trunc(fp128); // rounding to 0
fp128 fp128_remquo(fp128, fp128, int *); // calculate the remainder and quotient
long int fp128_lrint(fp128); // similar to rint
long long int fp128_llrint(fp128); // similar to rint
long int fp128_lround(fp128); // similar to round
long long int fp128_llround(fp128); // similar to round
fp128 fp128_fdim(fp128, fp128); // fmaxl(x-y, 0.0), faster
fp128 fp128_fmax(fp128, fp128); 
fp128 fp128_fmin(fp128, fp128);
fp128 fp128_fma(fp128, fp128, fp128);

#endif

