#include <stdio.h>
#include <stdint.h>
//#include "dd_real.h"
//#include "dd_real.c"
#include "ldouble.h"


#ifndef fp128
#define fp128 long double
#endif

typedef union {
	fp128 ld;
	dd_real dd;
	uint64_t ul[2];
} ltd;

#define D1(x, f) {   					\
	ltd ta, tb;					\
	ta.ld = x;					\
        tb.dd = f##_dd(ta.dd);				\
	return tb.ld;					\
}

#define D1DFM(x, f) {   				\
        return f(DDToFP128(x));				\
}

#define D1FM(x, f) {   					\
        return FP128ToDD(f(DDToFP128(x)));		\
}

#define D2PFM(a, b, f) {   				\
	fp128 x, y;					\
	x =  FP128ToDD(f(DDToFP128(a), &y));		\
	*b = FP128ToDD(y);				\
        return x;					\
}

#define D2JFM(x, y, f) {   				\
        return FP128ToDD(f(x, DDToFP128(y)));		\
}

#define D2IFM(x, y, f) {   				\
        return FP128ToDD(f(DDToFP128(x), y));		\
}

#define D2FM(x, y, f) {   				\
        return FP128ToDD(f(DDToFP128(x), 		\
		DDToFP128(y)));				\
}

#define D2(x, y, f) {   				\
	ltd ta, tb, tc;					\
	ta.ld = x;					\
	tb.ld = y;					\
        tc.dd = f##_dd_dd(ta.dd, tb.dd);		\
	return tc.ld;					\
}

#define D2F(x, y, f) {   				\
	ltd ta, tb;					\
	ta.ld = x;					\
	tb.ld = y;					\
        return f##_dd_dd(ta.dd, tb.dd);			\
}

#define D2I(x, y, f) {   				\
	ltd ta, tb;					\
	ta.ld = x;					\
        tb.dd = f##_dd_i(ta.dd, y);			\
	return tb.ld;					\
}

#define D3IFM(x, y, z, f) {   				\
        return FP128ToDD(f(DDToFP128(x), 		\
		DDToFP128(y), z));			\
}

//#define D3(x, y, z, f) {   				\
//	ltd ta, tb, tc, td;				\
//	ta.ld = x;					\
//	tb.ld = y;					\
//	tc.ld = z;					\
//        td.dd = f##_dd_dd_dd(ta.dd, tb.dd, tc.dd);	\
//	return td.ld;					\
//}

#define D3P(x, y, z, f) {   				\
	ltd ta, tb, tc, td;				\
	ta.ld = x;					\
	tb.ld = y;					\
	tc.ld = *z;					\
        td.dd = f##_dd_dd(ta.dd, tb.dd, &tc.dd);	\
	*z = tc.ld;					\
	return td.ld;					\
}

#define D3P2(x, y, z, f) {   				\
	ltd ta, tb, tc;					\
	ta.ld = x;					\
	tb.ld = *y;					\
	tc.ld = *z;					\
        f##_dd(ta.dd, &tb.dd, &tc.dd);			\
	*y = tb.ld;					\
	*z = tc.ld;					\
}

// FP128
inline void FP128ToDD0(fp128 a, fp128 *r) {
	ltd t;

	t.dd.x[0] = (double)a;
	t.dd.x[1] = (double)(a - (fp128)t.dd.x[0]);
	//printf("In FP128ToDD %lf %lf\n", t.dd.x[0], t.dd.x[1])
	DP("FP128ToDD", t);

	*r = t.ld;
}

void dd_printf(fp128 a) {
	fp128 t;
	FP128ToDD0(a, &t);

	printf("%Lf\n", t);
}

inline fp128 DDToFP128(fp128 a) {
	ltd t;

	t.ld = a;
	DP("DDToFP128", t);
	
	return ((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline double DDToFP64(fp128 a) {
	ltd t;
	t.ld = a;
	DP("DDToDouble", t);
	
	return t.dd.x[0];
}

inline float DDToFP32(fp128 a) {
	ltd t;
	t.ld = a;

	//return (float)(t.dd.x[0] + t.dd.x[1]);
	return (float)(t.dd.x[0]);
}

inline __fp16 DDToFP16(fp128 a) {
	ltd t;
	t.ld = a;

	//return (float)(t.dd.x[0] + t.dd.x[1]);
	return (__fp16)(t.dd.x[0]);
}

inline __bf16 DDToBP16(fp128 a) {
	ltd t;
	t.ld = a;

	//return (float)(t.dd.x[0] + t.dd.x[1]);
	return FP64ToBF16(t.dd.x[0]);
}

inline fp128 FP128ToDD(fp128 a) {
	fp128 t;
	FP128ToDD0(a, &t);
	return t;
}

inline fp128 FP64ToDD(double a) {
	ltd t;
	t.dd = dd_init(a);
	DP("DoubleToDD", t);

	return t.ld;
}

inline fp128 FP32ToDD(float a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 FP16ToDD(__fp16 a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 BF16ToDD(__bf16 a) {
	ltd t;
	t.dd = dd_init(BF16ToFP64(a));

	return t.ld;
}

inline uint32_t DDToU32(fp128 a) {
	ltd t;
	t.ld = a;

	return (uint32_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline uint64_t DDToU64(fp128 a) {
	ltd t;
	t.ld = a;

	return (uint64_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline int32_t DDToS32(fp128 a) {
	ltd t;
	t.ld = a;

	return (int32_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline int64_t DDToS64(fp128 a) {
	ltd t;
	t.ld = a;

	return (int64_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline fp128 U32ToDD(uint32_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 U64ToDD(uint64_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 S32ToDD(int32_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 S64ToDD(int64_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 fp128_muladd222(fp128 a, fp128 b, fp128 c) {
	ltd ta, tb, tc, r;
	ta.ld = a;
	tb.ld = b;
	tc.ld = c;

	DP("fp128_muladd222 a", ta);
	DP("fp128_muladd222 b", tb);
	DP("fp128_muladd222 c", tc);
	r.dd = add_dd_dd(mul_dd_dd(ta.dd, tb.dd), tc.dd);
	DP("fp128_muladd222 r", r);

	return r.ld;
}

inline int fp128_fpclassify(fp128 a)				D1FM(a, __fpclassifyl)
inline int fp128_signbit(fp128 a)				D1FM(a, __signbitl)
inline int fp128_isinf(fp128 a)					D1FM(a, __isinfl)
inline int fp128_isnan(fp128 a)					D1FM(a, __isnanl)
inline int fp128_finite(fp128 a)				D1FM(a, __finitel)
inline int fp128_iseqsig(fp128 a, fp128 b)			D2FM(a, b, __iseqsigl)
inline int fp128_issignaling(fp128 a)				D1FM(a, __issignalingl)

inline fp128 fp128_floor(fp128 a)				D1(a, floor)
inline fp128 fp128_ceil(fp128 a)				D1(a, ceil)

inline fp128 fp128_add22(fp128 a, fp128 b)			D2(a, b, add) 
inline fp128 fp128_sub22(fp128 a, fp128 b)			D2(a, b, sub)
inline fp128 fp128_mul22(fp128 a, fp128 b)			D2(a, b, mul)
inline fp128 fp128_div22(fp128 a, fp128 b)			D2(a, b, div)

inline fp128 fp128_neg(fp128 a)					D1(a, neg) 

inline bool fp128_eq(fp128 a, fp128 b)				D2F(a, b, eq)
inline bool fp128_le(fp128 a, fp128 b)				D2F(a, b, le)
inline bool fp128_ge(fp128 a, fp128 b)				D2F(a, b, ge)
inline bool fp128_lt(fp128 a, fp128 b)				D2F(a, b, lt)
inline bool fp128_gt(fp128 a, fp128 b)				D2F(a, b, gt)
inline bool fp128_ne(fp128 a, fp128 b)				D2F(a, b, ne) 

inline fp128 fp128_sin(fp128 a)					D1(a, sin) 
inline fp128 fp128_cos(fp128 a)					D1(a, cos) 
inline fp128 fp128_tan(fp128 a)					D1(a, tan) 
inline void fp128_sincos(fp128 a, fp128 *sin_a, fp128 *cos_a)	D3P2(a, sin_a, cos_a, sincos)
inline fp128 fp128_asin(fp128 a)				D1(a, asin)
inline fp128 fp128_acos(fp128 a)				D1(a, acos) 
inline fp128 fp128_atan(fp128 a)				D1(a, atan) 
inline fp128 fp128_atan2(fp128 a, fp128 b)			D2(a, b, atan2)
inline fp128 fp128_sinh(fp128 a)				D1(a, sinh)
inline fp128 fp128_cosh(fp128 a)				D1(a, cosh)
inline fp128 fp128_tanh(fp128 a)				D1(a, tanh)
inline void fp128_sincosh(fp128 a, fp128 *s, fp128 *c)		D3P2(a, s, c, sincosh)
inline fp128 fp128_asinh(fp128 a)				D1(a, asinh)
inline fp128 fp128_acosh(fp128 a)				D1(a, acosh)
inline fp128 fp128_atanh(fp128 a)				D1(a, atanh)

inline fp128 fp128_exp(fp128 a)					D1(a, exp)
inline fp128 fp128_ldexp(fp128 a, int exp)			D2I(a, exp, ldexp)
inline fp128 fp128_expm1(fp128 a)				D1FM(a, expm1l)
inline fp128 fp128_frexp(fp128 a, int *e)			D2IFM(a, e, frexpl)
inline fp128 fp128_exp2(fp128 a)				D1FM(a, exp2l)

inline fp128 fp128_log(fp128 a)					D1(a, log)
inline fp128 fp128_log10(fp128 a)				D1(a, log10) 
inline fp128 fp128_log1p(fp128 a)				D1FM(a, log1pl)
inline fp128 fp128_logb(fp128 a)				D1FM(a, logbl) 
inline fp128 fp128_log2(fp128 a) 				D1FM(a, log2l)

inline fp128 fp128_drem(fp128 a, fp128 b)			D2(a, b, drem)
inline fp128 fp128_divrem(fp128 a, fp128 b, fp128 *r)		D3P(a, b, r, divrem)
inline fp128 fp128_modf(fp128 a, fp128 *r)			D2PFM(a, r, modfl) 
inline fp128 fp128_fmod(fp128 a, fp128 b)			D2FM(a, b, fmodl)

inline fp128 fp128_pow(fp128 a, fp128 b)			D2(a, b, pow)
inline fp128 fp128_sqr(fp128 a)					D1(a, sqr) 
inline fp128 fp128_sqrt(fp128 a)				D1(a, sqrt)

#define fp128_fabs fp128_abs
inline fp128 fp128_abs(fp128 a)					D1(a, abs)
//fp128 fp128_inv(fp128 a)					D1(a, inv)
//inline fp128 fp128_npwr(fp128 a, int n)				D2I(a, n, npwr)
//inline fp128 fp128_nroot(fp128 a, int n)			D2I(a, n, nroot)
//inline fp128 fp128_nint(fp128 a)				D1(a, nint)
//inline fp128 fp128_aint(fp128 a)				D1(a, aint)	

inline fp128 fp128_hypot(fp128 a, fp128 b)			D2FM(a, b, hypotl)
inline fp128 fp128_cbrt(fp128 a)				D1FM(a, cbrtl)
inline fp128 fp128_significand(fp128 a)				D1FM(a, significandl)
inline fp128 fp128_copysign(fp128 a, fp128 b)			D2FM(a, b, copysignl)
inline fp128 fp128_nan(char *c)					{return FP128ToDD(nanl(c));}			
inline fp128 fp128_j0(fp128 a)					D1FM(a, j0l)
inline fp128 fp128_j1(fp128 a)					D1FM(a, j1l)
inline fp128 fp128_jn(int n, fp128 a)				D2JFM(n, a, jnl)
inline fp128 fp128_y0(fp128 a)					D1FM(a, y0l)
inline fp128 fp128_y1(fp128 a)					D1FM(a, y1l)
inline fp128 fp128_yn(int n, fp128 a)				D2JFM(n, a, ynl)
inline fp128 fp128_erf(fp128 a)					D1FM(a, erfl)
inline fp128 fp128_erfc(fp128 a)				D1FM(a, erfcl)
inline fp128 fp128_lgamma(fp128 a)				D1FM(a, lgammal)
inline fp128 fp128_tgamma(fp128 a)				D1FM(a, tgammal)
inline fp128 fp128_gamma(fp128 a)				D1FM(a, gammal)
inline fp128 fp128_lgamma_r(fp128 a, int *s)			D2IFM(a, s, lgammal_r)
inline fp128 fp128_rint(fp128 a)				D1(a, nint)
inline fp128 fp128_nextafter(fp128 a, fp128 b)			D2FM(a, b, nextafterl)
inline fp128 fp128_nexttoward(fp128 a, fp128 b)			D2FM(a, b, nexttowardl)
inline fp128 fp128_scalbn(fp128 a, int n)			D2IFM(a, n, scalbnl)
inline int fp128_ilogb(fp128 a)					D1DFM(a, ilogbl)
inline fp128 fp128_nearbyint(fp128 a)				D1FM(a, nearbyintl)
inline fp128 fp128_round(fp128 a)				D1FM(a, roundl)
inline fp128 fp128_trunc(fp128 a)				D1FM(a, truncl)
inline fp128 fp128_remquo(fp128 a, fp128 b, int *q)		D3IFM(a, b, q, remquol)
inline long int fp128_lrint(fp128 a)				D1DFM(a, lrintl)
inline long long int fp128_llrint(fp128 a)			D1DFM(a, llrintl)
inline long int fp128_lround(fp128 a)				D1DFM(a, lroundl)
inline long long int fp128_llround(fp128 a)			D1DFM(a, llroundl)
inline fp128 fp128_fdim(fp128 a, fp128 b)			D2FM(a, b, fdiml)
inline fp128 fp128_fmax(fp128 a, fp128 b)			D2FM(a, b, fmaxl) 
inline fp128 fp128_fmin(fp128 a, fp128 b)			D2FM(a, b, fminl)

#define fp128_fma fp128_muladd222
//inline fp128 fp128_fma(fp128, fp128, fp128)


// check 
