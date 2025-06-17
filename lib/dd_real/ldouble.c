#include <stdio.h>
#include <stdint.h>
#include "dd_real.h"
#include "ldouble.h"


#ifndef fp128
#define fp128 long double
#endif

typedef union {
	fp128 ld;
	dd_real dd;
	uint64_t ul[2];
} ltd;

// FP128
inline void FP128ToDD(fp128 a, fp128 *r) {
	ltd t;

	t.dd.x[0] = (double)a;
	t.dd.x[1] = (double)(a - (fp128)t.dd.x[0]);
	//printf("In FP128ToDD %lf %lf\n", t.dd.x[0], t.dd.x[1])
	DP("FP128ToDD", t);

	*r = t.ld;
}

inline fp128 FP128ToDD1(fp128 a) {
	fp128 t;
	FP128ToDD(a, &t);
	return t;
}

fp128 DDToFP128(fp128 a) {
	ltd t;

	t.ld = a;
	//printf("%lf, %lf", t.dd.x[0], t.dd.x[1]);
	DP("DDToFP128", t);
	
	return ((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

void fp128_printfp128(fp128 a) {
	ltd t;
	t.ld = a;

	printf("%lx %lx\n", t.ul[0], t.ul[1]);
}

void fp128_printdd(fp128 a) {
	ltd t;
	t.ld = DDToFP128(a);

	printf("%lx %lx\n", t.ul[0], t.ul[1]);
}

void fp128_printd(double a) {
	printf("%lx\n", *(uint64_t *)(&a));
}

void fp128_printf(float a) {
	printf("%x\n", *(uint32_t *)(&a));
}

double DDToDouble(fp128 a) {
	ltd t;
	t.ld = a;
	DP("DDToDouble", t);
	
	return t.dd.x[0];
}

fp128 DoubleToDD(double a) {
	ltd t;
	t.dd = dd_init(a);
	DP("DoubleToDD", t);

	return t.ld;
}

float DDToFloat(fp128 a) {
	ltd t;
	t.ld = a;

	//return (float)(t.dd.x[0] + t.dd.x[1]);
	return (float)(t.dd.x[0]);
}

fp128 FloatToDD(float a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

uint32_t DDToU32(fp128 a) {
	ltd t;
	t.ld = a;

	return (uint32_t)(t.dd.x[0]);
}

uint64_t DDToU64(fp128 a) {
	ltd t;
	t.ld = a;

	return (uint64_t)(t.dd.x[0]);
}

int32_t DDToS32(fp128 a) {
	ltd t;
	t.ld = a;

	return (int32_t)(t.dd.x[0]);
}

int64_t DDToS64(fp128 a) {
	ltd t;
	t.ld = a;

	return (int64_t)(t.dd.x[0]);
}

fp128 U32ToDD(uint32_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

fp128 U64ToDD(uint64_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

fp128 S32ToDD(int32_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

fp128 S64ToDD(int64_t a) {
	ltd t;
	t.dd = dd_init((double)a);

	return t.ld;
}

inline fp128 fp128_add22(fp128 a, fp128 b) {
	//printf("in fp128_add\n");
	ltd ta, tb, tc;
	ta.ld = a;
	tb.ld = b;
	
	tc.dd = add_dd_dd(ta.dd, tb.dd);
	DP("fp128_add22", tc);

	return tc.ld;
}

fp128 fp128_add2c2(fp128 a, fp128 b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_add22(aa, b);	
}

fp128 fp128_add22c(fp128 a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_add22(a, bb);
}

fp128 fp128_add2c2c(fp128 a, fp128 b) {
	fp128 aa, bb;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	return fp128_add22(aa, bb);
}

inline fp128 fp128_add12(double a, fp128 b) {
	ltd tb, tc;
	tb.ld = b;
	
	tc.dd = add_d_dd(a, tb.dd);
	DP("fp128_add12", tc);

	return tc.ld;
}

fp128 fp128_add12c(double a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_add12(a, bb);
}

inline fp128 fp128_add21(fp128 a, double b) {
	ltd ta, tc;
	ta.ld = a;
	
	tc.dd = add_dd_d(ta.dd, b);
	DP("fp128_add21", tc);

	return tc.ld;
}

fp128 fp128_add2c1(fp128 a, double b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_add21(aa, b);
}

fp128 fp128_add11(double a, double b) {
	ltd tc;
	
	tc.dd = add_d_d(a, b);
	DP("fp128_add21", tc);

	return tc.ld;
}

inline fp128 fp128_sub22(fp128 a, fp128 b) {
	ltd ta, tb, tc;
	ta.ld = a;
	tb.ld = b;

	DP("fp128_sub22 ta", ta);
	DP("fp128_sub22 tb", tb);
	tc.dd = sub_dd_dd(ta.dd, tb.dd);
	DP("fp128_sub22 tc", tc);

	return tc.ld;
}

fp128 fp128_sub2c2(fp128 a, fp128 b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_sub22(aa, b);	
}

fp128 fp128_sub22c(fp128 a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_sub22(a, bb);
}

fp128 fp128_sub2c2c(fp128 a, fp128 b) {
	fp128 aa, bb;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	return fp128_sub22(aa, bb);
}

inline fp128 fp128_sub12(double a, fp128 b) {
	ltd tb, tc;
	tb.ld = b;

	tc.dd = sub_d_dd(a, tb.dd);
	DP("fp128_sub12", tc);

	return tc.ld;
}

fp128 fp128_sub12c(double a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_sub12(a, bb);
}

inline fp128 fp128_sub21(fp128 a, double b) {
	ltd ta, tc;
	ta.ld = a;

	tc.dd = sub_dd_d(ta.dd, b);
	DP("fp128_sub21", tc);

	return tc.ld;
}

fp128 fp128_sub2c1(fp128 a, double b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_sub21(aa, b);
}

fp128 fp128_sub11(double a, double b) {
	ltd tc;

	tc.dd = sub_d_d(a, b);
	DP("fp128_sub21", tc);

	return tc.ld;
}

inline fp128 fp128_mul22(fp128 a, fp128 b) {
	ltd ta, tb, tc;
	ta.ld = a;
	tb.ld = b;

	tc.dd = mul_dd_dd(ta.dd, tb.dd);
	DP("fp128_mul22", tc);

	return tc.ld;
}

fp128 fp128_mul2c2(fp128 a, fp128 b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_mul22(aa, b);	
}

fp128 fp128_mul22c(fp128 a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_mul22(a, bb);
}

fp128 fp128_mul2c2c(fp128 a, fp128 b) {
	fp128 aa, bb;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	return fp128_mul22(aa, bb);
}

inline fp128 fp128_mul12(double a, fp128 b) {
	ltd tb, tc;
	tb.ld = b;

	tc.dd = mul_d_dd(a, tb.dd);
	DP("fp128_mul12", tc);

	return tc.ld;
}

fp128 fp128_mul12c(double a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_mul12(a, bb);
}

inline fp128 fp128_mul21(fp128 a, double b) {
	ltd ta, tc;
	ta.ld = a;

	tc.dd = mul_dd_d(ta.dd, b);
	DP("fp128_mul21", tc);

	return tc.ld;
}

fp128 fp128_mul2c1(fp128 a, double b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_mul21(aa, b);
}

fp128 fp128_mul11(double a, double b) {
	ltd tc;

	tc.dd = mul_d_d(a, b);
	DP("fp128_mul21", tc);

	return tc.ld;
}

inline fp128 fp128_div22(fp128 a, fp128 b) {
	ltd ta, tb, tc;
	ta.ld = a;
	tb.ld = b;
	DP("fp128_div22 pre", ta);
	DP("fp128_div22 pre", tb);

	tc.dd = div_dd_dd(ta.dd, tb.dd);
	DP("fp128_div22", tc);

	return tc.ld;
}

fp128 fp128_div2c2(fp128 a, fp128 b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_div22(aa, b);	
}

fp128 fp128_div22c(fp128 a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_div22(a, bb);
}

fp128 fp128_div2c2c(fp128 a, fp128 b) {
	fp128 aa, bb;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	return fp128_div22(aa, bb);
}

inline fp128 fp128_div12(double a, fp128 b) {
	ltd tb, tc;
	tb.ld = b;
	DP("fp128_div12 pre", tb);

	tc.dd = div_d_dd(a, tb.dd);
	DP("fp128_div12", tc);

	return tc.ld;
}

fp128 fp128_div12c(double a, fp128 b) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_div12(a, bb);
}

inline fp128 fp128_div21(fp128 a, double b) {
	ltd ta, tc;
	ta.ld = a;
	DP("fp128_div21 pre", ta);

	tc.dd = div_dd_d(ta.dd, b);
	DP("fp128_div21", tc);

	return tc.ld;
}

fp128 fp128_div2c1(fp128 a, double b) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_div21(aa, b);
}

fp128 fp128_div11(double a, double b) {
	ltd tc;

	tc.dd = div_d_d(a, b);
	DP("fp128_div11", tc);

	return tc.ld;
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

fp128 fp128_muladd2c22(fp128 a, fp128 b, fp128 c) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_muladd222(aa, b, c);
}

fp128 fp128_muladd22c2(fp128 a, fp128 b, fp128 c) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_muladd222(a, bb, c);
}

fp128 fp128_muladd222c(fp128 a, fp128 b, fp128 c) {
	fp128 cc;
	FP128ToDD(c, &cc);
	return fp128_muladd222(a, b, cc);
}

fp128 fp128_muladd2c2c2(fp128 a, fp128 b, fp128 c) {
	fp128 aa, bb;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	return fp128_muladd222(aa, bb, c);
}

fp128 fp128_muladd2c22c(fp128 a, fp128 b, fp128 c) {
	fp128 aa, cc;
	FP128ToDD(a, &aa);
	FP128ToDD(c, &cc);
	return fp128_muladd222(aa, b, cc);
}

fp128 fp128_muladd22c2c(fp128 a, fp128 b, fp128 c) {
	fp128 bb, cc;
	FP128ToDD(b, &bb);
	FP128ToDD(c, &cc);
	return fp128_muladd222(a, bb, cc);
}

fp128 fp128_muladd2c2c2c(fp128 a, fp128 b, fp128 c) {
	fp128 aa, bb, cc;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	FP128ToDD(c, &cc);
	return fp128_muladd222(aa, bb, cc);
}

inline fp128 fp128_muladd211(fp128 a, double b, double c) {
        ltd ta, r;
        ta.ld = a;

        r.dd = add_dd_d(mul_dd_d(ta.dd, b), c);
        DP("fp128_muladd211", r);

        return r.ld;
}

fp128 fp128_muladd2c11(fp128 a, double b, double c) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_muladd211(aa, b, c);
}

inline fp128 fp128_muladd121(double a, fp128 b, double c) {
        ltd tb, r;
        tb.ld = b;

        r.dd = add_dd_d(mul_d_dd(a, tb.dd), c);
        DP("fp128_muladd121", r);

        return r.ld;
}

fp128 fp128_muladd12c1(double a, fp128 b, double c) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_muladd121(a, bb, c);
}

inline fp128 fp128_muladd112(double a, double b, fp128 c) {
        ltd tc, r;
        tc.ld = c;

        r.dd = add_dd_dd(mul_d_d(a, b), tc.dd);
        DP("fp128_muladd112", r);

        return r.ld;
}

fp128 fp128_muladd112c(double a, double b, fp128 c) {
	fp128 cc;
	FP128ToDD(c, &cc);
	return fp128_muladd112(a, b, cc);
}

inline fp128 fp128_muladd122(double a, fp128 b, fp128 c) {
	ltd tb, tc, r;
	tb.ld = b;
	tc.ld = c;

	r.dd = add_dd_dd(mul_d_dd(a, tb.dd), tc.dd);
	DP("fp128_muladd122", r);

	return r.ld;
}

fp128 fp128_muladd12c2(double a, fp128 b, fp128 c) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_muladd122(a, bb, c);
}

fp128 fp128_muladd122c(double a, fp128 b, fp128 c) {
	fp128 cc;
	FP128ToDD(c, &cc);
	return fp128_muladd122(a, b, cc);
}

fp128 fp128_muladd12c2c(double a, fp128 b, fp128 c) {
	fp128 bb, cc;
	FP128ToDD(b, &bb);
	FP128ToDD(c, &cc);
	return fp128_muladd122(a, bb, cc);
}

inline fp128 fp128_muladd212(fp128 a, double b, fp128 c) {
	ltd ta, tc, r;
	ta.ld = a;
	tc.ld = c;

	r.dd = add_dd_dd(mul_dd_d(ta.dd, b), tc.dd);
	DP("fp128_muladd212", r);

	return r.ld;
}

fp128 fp128_muladd2c12(fp128 a, double b, fp128 c) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_muladd211(aa, b, c);
}

fp128 fp128_muladd212c(fp128 a, double b, fp128 c) {
	fp128 cc;
	FP128ToDD(c, &cc);
	return fp128_muladd212(a, b, cc);
}

fp128 fp128_muladd2c12c(fp128 a, double b, fp128 c) {
	fp128 aa, cc;
	FP128ToDD(a, &aa);
	FP128ToDD(c, &cc);
	return fp128_muladd212(aa, b, cc);
}

inline fp128 fp128_muladd221(fp128 a, fp128 b, double c) {
	ltd ta, tb, r;
	ta.ld = a;
	tb.ld = b;

	r.dd = add_dd_d(mul_dd_dd(ta.dd, tb.dd), c);
	DP("fp128_muladd221", r);

	return r.ld;
}

fp128 fp128_muladd2c21(fp128 a, fp128 b, double c) {
	fp128 aa;
	FP128ToDD(a, &aa);
	return fp128_muladd221(aa, b, c);
}

fp128 fp128_muladd22c1(fp128 a, fp128 b, double c) {
	fp128 bb;
	FP128ToDD(b, &bb);
	return fp128_muladd221(a, bb, c);
}

fp128 fp128_muladd2c2c1(fp128 a, fp128 b, double c) {
	fp128 aa, bb;
	FP128ToDD(a, &aa);
	FP128ToDD(b, &bb);
	return fp128_muladd221(aa, bb, c);
}

fp128 fp128_muladd111(double a, double b, double c) {
        ltd r;

        r.dd = add_dd_d(mul_d_d(a, b), c);
        DP("fp128_muladd111", r);

        return r.ld;
}

fp128 fp128_neg(fp128 a) {
	ltd ta, r;
	ta.ld = a;

	r.dd = neg_dd(ta.dd);
        DP("fp128_neg", r);
	return r.ld;
}

bool fp128_eq(fp128 a, fp128 b) {
	ltd ta, tb;
	ta.ld = a;
	tb.ld = b;

	return eq_dd_dd(ta.dd, tb.dd);
}

bool fp128_le(fp128 a, fp128 b) {
	ltd ta, tb;
	ta.ld = a;
	tb.ld = b;

	return le_dd_dd(ta.dd, tb.dd);
}

bool fp128_ge(fp128 a, fp128 b) {
	ltd ta, tb;
	ta.ld = a;
	tb.ld = b;

	return ge_dd_dd(ta.dd, tb.dd);
}

bool fp128_lt(fp128 a, fp128 b) {
	ltd ta, tb;
	ta.ld = a;
	tb.ld = b;
	DP("fp128_lt", ta);
	DP("fp128_lt", tb);

	return lt_dd_dd(ta.dd, tb.dd);
}

bool fp128_gt(fp128 a, fp128 b) {
	ltd ta, tb;
	ta.ld = a;
	tb.ld = b;

	return gt_dd_dd(ta.dd, tb.dd);
}

bool fp128_ne(fp128 a, fp128 b) {
	ltd ta, tb;
	ta.ld = a;
	tb.ld = b;

	return ne_dd_dd(ta.dd, tb.dd);
}

fp128 fp128_exp(fp128 a) {
	ltd ta, tc;
	ta.ld = a;

	tc.dd = exp_dd(ta.dd);
        DP("fp128_exp", tc);

	return tc.ld;
}

fp128 fp128_pow(fp128 a, fp128 b) {
	ltd ta, tb, tc;
	ta.ld = a;
	tb.ld = b;

	tc.dd = pow_dd_dd(ta.dd, tb.dd);

	return 	tc.ld;
}

fp128 fp128_sqr(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = sqr_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_sqrt(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = sqrt_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_log(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = log_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_log10(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = log10_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_sin(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = sin_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_cos(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = cos_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_tan(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = tan_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_asin(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = asin_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_acos(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = acos_dd(ta.dd);

	return 	tb.ld;
}

fp128 fp128_atan(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = atan_dd(ta.dd);

	return 	tb.ld;
}

#define fp128_abs fp128_fabs
fp128 fp128_fabs(fp128 a) {
	ltd ta, tb;
	ta.ld = a;

	tb.dd = abs_dd(ta.dd);

	return 	tb.ld;
}

// check 
