#include "ibm128.h"

typedef union {
	double2 d2;
	dd_real dd;
	uint64_t ul[2];
} ibmtdd;

// ibm128
void ppc_printf(double2 a) {
        ibmtdd t;
        t.d2 = a;

        printf("%lx %lx\n", t.ul[0], t.ul[1]);
}

inline dd_real init_ppctodd(double2 a) {
	ibmtdd t;
	t.d2 = a;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return t.dd;
#else
	return dd_init2(t.dd.x[1], t.dd.x[0]);
#endif
}

inline double2 init_ddtoppc(dd_real a) {
	ibmtdd t;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	t.dd = a
#else
	t.dd = dd_init2(a.x[1], a.x[0]);
#endif
	return t.d2;
}

//cast
inline long double PPCToFP128(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return ((long double)t.dd.x[0] + (long double)t.dd.x[1]);
}

inline double PPCToFP64(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return t.dd.x[0];
}

inline float  PPCToFP32(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return (float)t.dd.x[0];
}

inline __fp16 PPCToFP16(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return (__fp16)t.dd.x[0];
}

inline __bf16 PPCToBF16(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return FP64ToBF16(t.dd.x[0]);
}

inline double2 FP128ToPPC(long double a) {
	ibmtdd t;
	t.dd.x[0] = (double)a;
	t.dd.x[1] = (double)(a - (long double)t.dd.x[0]);
	return t.d2;
}

inline double2 FP64ToPPC(double a) {
	ibmtdd t;
	t.dd.x[0] = a;
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 FP32ToPPC(float  a) {
	ibmtdd t;
	t.dd.x[0] = (double)a;
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 FP16ToPPC(__fp16 a) {
	ibmtdd t;
	t.dd.x[0] = (double)a;
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 BF16ToPPC(__bf16 a) {
	ibmtdd t;
	t.dd.x[0] = BF16ToFP64(a);
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline uint32_t PPCToU32(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return (uint32_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline int32_t  PPCToS32(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return (int32_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline uint64_t PPCToU64(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return (uint64_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline int64_t  PPCToS64(double2 a) {
	ibmtdd t;
	t.d2 = a;
	return (int64_t)((fp128)t.dd.x[0] + (fp128)t.dd.x[1]);
}

inline double2 U32ToPPC(uint32_t a) {
	ibmtdd t;
	t.dd.x[0] = (double)(a);
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 S32ToPPC(int32_t  a) {
	ibmtdd t;
	t.dd.x[0] = (double)(a);
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 U64ToPPC(uint64_t a) {
	ibmtdd t;
	t.dd.x[0] = (double)(a);
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 S64ToPPC(int64_t  a) {
	ibmtdd t;
	t.dd.x[0] = (double)(a);
	t.dd.x[1] = 0.0;
	return t.d2;
}

inline double2 ppc_muladd222(double2 a, double2 b, double2 c) {
        ibmtdd r;

        r.dd = add_dd_dd(mul_dd_dd(init_ppctodd(a), init_ppctodd(b)), init_ppctodd(c));
        DP("fp128_muladd222 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_muladd221(double2 a, double2 b, double c) {
        ibmtdd r;

        r.dd = add_dd_d(mul_dd_dd(init_ppctodd(a), init_ppctodd(b)), c);
        DP("fp128_muladd221 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_muladd212(double2 a, double b, double2 c) {
        ibmtdd r;

        r.dd = add_dd_dd(mul_dd_d(init_ppctodd(a), b), init_ppctodd(c));
        DP("fp128_muladd212 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_muladd122(double a, double2 b, double2 c) {
        ibmtdd r;

        r.dd = add_dd_dd(mul_d_dd(a, init_ppctodd(b)), init_ppctodd(c));
        DP("fp128_muladd122 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_muladd211(double2 a, double b, double c) {
        ibmtdd r;

        r.dd = add_dd_d(mul_dd_d(init_ppctodd(a), b), c);
        DP("fp128_muladd211 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_muladd121(double a, double2 b, double c) {
        ibmtdd r;

        r.dd = add_dd_d(mul_d_dd(a, init_ppctodd(b)), c);
        DP("fp128_muladd121 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_muladd112(double a, double b, double2 c) {
        ibmtdd r;

        r.dd = add_dd_dd(mul_d_d(a, b), init_ppctodd(c));
        DP("fp128_muladd112 r", r);

	return init_ddtoppc(r.dd);
}

inline double2 ppc_add22(double2 a, double2 b) {
        ibmtdd tc;

        tc.dd = add_dd_dd(init_ppctodd(a), init_ppctodd(b));
        DP("ppc_add22", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_add21(double2 a, double b) {
        ibmtdd tc;

        tc.dd = add_dd_d(init_ppctodd(a), b);
        DP("ppc_add21", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_add12(double a, double2 b) {
        ibmtdd tc;

        tc.dd = add_d_dd(a, init_ppctodd(b));
        DP("ppc_add12", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_sub22(double2 a, double2 b) {
        ibmtdd tc;

        tc.dd = sub_dd_dd(init_ppctodd(a), init_ppctodd(b));
        DP("ppc_sub22 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_sub21(double2 a, double b) {
        ibmtdd tc;

        tc.dd = sub_dd_d(init_ppctodd(a), b);
        DP("ppc_sub21 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_sub12(double a, double2 b) {
        ibmtdd tc;

        tc.dd = sub_d_dd(a, init_ppctodd(b));
        DP("ppc_sub12 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_mul22(double2 a, double2 b) {
        ibmtdd tc;

        tc.dd = mul_dd_dd(init_ppctodd(a), init_ppctodd(b));
        DP("ppc_mul22 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_mul21(double2 a, double b) {
        ibmtdd tc;

        tc.dd = mul_dd_d(init_ppctodd(a), b);
        DP("ppc_mul21 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_mul12(double a, double2 b) {
        ibmtdd tc;

        tc.dd = mul_d_dd(a, init_ppctodd(b));
        DP("ppc_mul12 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_div22(double2 a, double2 b) {
        ibmtdd tc;

        tc.dd = div_dd_dd(init_ppctodd(a), init_ppctodd(b));
        DP("ppc_div22 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_div21(double2 a, double b) {
        ibmtdd tc;

        tc.dd = div_dd_d(init_ppctodd(a), b);
        DP("ppc_div21 tc", tc);

        return init_ddtoppc(tc.dd);
}

inline double2 ppc_div12(double a, double2 b) {
        ibmtdd tc;

        tc.dd = div_d_dd(a, init_ppctodd(b));
        DP("ppc_div12 tc", tc);

        return init_ddtoppc(tc.dd);
}

bool ppc_eq(double2 a, double2 b) {
        return eq_dd_dd(init_ppctodd(a), init_ppctodd(b));
}

bool ppc_le(double2 a, double2 b) {
        return le_dd_dd(init_ppctodd(a), init_ppctodd(b));
}

bool ppc_ge(double2 a, double2 b) {
        return ge_dd_dd(init_ppctodd(a), init_ppctodd(b));
}

bool ppc_lt(double2 a, double2 b) {
        return lt_dd_dd(init_ppctodd(a), init_ppctodd(b));
}

bool ppc_gt(double2 a, double2 b) {
        return gt_dd_dd(init_ppctodd(a), init_ppctodd(b));
}

bool ppc_ne(double2 a, double2 b) {
        return ne_dd_dd(init_ppctodd(a), init_ppctodd(b));
}

double2 ppc_exp(double2 a) {
	ibmtdd ta, tc;
	ta.d2 = a;

	tc.dd = exp_dd(ta.dd);
        DP("ppc_exp", tc);

	return tc.d2;
}

double2 ppc_pow(double2 a, double2 b) {
	ibmtdd ta, tb, tc;
	ta.d2 = a;
	tb.d2 = b;

	tc.dd = pow_dd_dd(ta.dd, tb.dd);

	return 	tc.d2;
}

double2 ppc_sqr(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = sqr_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_sqrt(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = sqrt_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_log(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = log_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_log10(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = log10_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_sin(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = sin_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_cos(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = cos_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_tan(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = tan_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_asin(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = asin_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_acos(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = acos_dd(ta.dd);

	return 	tb.d2;
}

double2 ppc_atan(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = atan_dd(ta.dd);

	return 	tb.d2;
}

#define ppc_abs ppc_fabs
double2 ppc_fabs(double2 a) {
	ibmtdd ta, tb;
	ta.d2 = a;

	tb.dd = abs_dd(ta.dd);

	return 	tb.d2;
}

