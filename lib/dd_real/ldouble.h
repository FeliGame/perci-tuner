#ifndef LDOUBLE_H
#define LDOUBLE_H

#include <stdbool.h>
#include <stdint.h>
//#include <math.h>
//#include <arm_neon.h>

//#ifndef __bf16
//#define __bf16 unsigned short
//#endif
#ifndef fp128
#define fp128 long double
#endif

void FP128ToDD(fp128 a, fp128 *r);
fp128 FP128ToDD1(fp128 a);
fp128 DDToFP128(fp128 a);
void fp128_printfp128(fp128 a);
void fp128_printdd(fp128 a);
double DDToDouble(fp128 a);
fp128 DoubleToDD(double a);
float DDToFloat(fp128 a);
fp128 FloatToDD(float a);
uint32_t DDToU32(fp128 a);
uint64_t DDToU64(fp128 a);
int32_t DDToS32(fp128 a);
int64_t DDToS64(fp128 a);
fp128 U32ToDD(uint32_t a);
fp128 U64ToDD(uint64_t a);
fp128 S32ToDD(int32_t a);
fp128 S64ToDD(int64_t a);

fp128 fp128_add22(fp128 a, fp128 b);
fp128 fp128_add2c2(fp128 a, fp128 b);
fp128 fp128_add22c(fp128 a, fp128 b);
fp128 fp128_add2c2c(fp128 a, fp128 b);
fp128 fp128_add12(double a, fp128 b);
fp128 fp128_add12c(double a, fp128 b);
fp128 fp128_add21(fp128 a, double b);
fp128 fp128_add2c1(fp128 a, double b);
fp128 fp128_add11(double a, double b);

fp128 fp128_sub22(fp128 a, fp128 b);
fp128 fp128_sub2c2(fp128 a, fp128 b);
fp128 fp128_sub22c(fp128 a, fp128 b);
fp128 fp128_sub2c2c(fp128 a, fp128 b);
fp128 fp128_sub12(double a, fp128 b);
fp128 fp128_sub12c(double a, fp128 b);
fp128 fp128_sub21(fp128 a, double b);
fp128 fp128_sub2c1(fp128 a, double b);
fp128 fp128_sub11(double a, double b);

fp128 fp128_mul22(fp128 a, fp128 b);
fp128 fp128_mul2c2(fp128 a, fp128 b);
fp128 fp128_mul22c(fp128 a, fp128 b);
fp128 fp128_mul2c2c(fp128 a, fp128 b);
fp128 fp128_mul12(double a, fp128 b);
fp128 fp128_mul12c(double a, fp128 b);
fp128 fp128_mul21(fp128 a, double b);
fp128 fp128_mul2c1(fp128 a, double b);
fp128 fp128_mul11(double a, double b);

fp128 fp128_div22(fp128 a, fp128 b);
fp128 fp128_div2c2(fp128 a, fp128 b);
fp128 fp128_div22c(fp128 a, fp128 b);
fp128 fp128_div2c2c(fp128 a, fp128 b);
fp128 fp128_div12(double a, fp128 b);
fp128 fp128_div12c(double a, fp128 b);
fp128 fp128_div21(fp128 a, double b);
fp128 fp128_div2c1(fp128 a, double b);
fp128 fp128_div11(double a, double b);

fp128 fp128_muladd222(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd2c22(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd22c2(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd222c(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd2c2c2(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd2c22c(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd22c2c(fp128 a, fp128 b, fp128 c);
fp128 fp128_muladd2c2c2c(fp128 a, fp128 b, fp128 c);

fp128 fp128_muladd211(fp128 a, double b, double c);
fp128 fp128_muladd2c11(fp128 a, double b, double c);

fp128 fp128_muladd121(double a, fp128 b, double c);
fp128 fp128_muladd12c1(double a, fp128 b, double c);

fp128 fp128_muladd112(double a, double b, fp128 c);
fp128 fp128_muladd112c(double a, double b, fp128 c);

fp128 fp128_muladd122(double a, fp128 b, fp128 c);
fp128 fp128_muladd12c2(double a, fp128 b, fp128 c);
fp128 fp128_muladd122c(double a, fp128 b, fp128 c);
fp128 fp128_muladd12c2c(double a, fp128 b, fp128 c);

fp128 fp128_muladd212(fp128 a, double b, fp128 c);
fp128 fp128_muladd2c12(fp128 a, double b, fp128 c);
fp128 fp128_muladd212c(fp128 a, double b, fp128 c);
fp128 fp128_muladd2c12c(fp128 a, double b, fp128 c);

fp128 fp128_muladd221(fp128 a, fp128 b, double c);
fp128 fp128_muladd2c21(fp128 a, fp128 b, double c);
fp128 fp128_muladd22c1(fp128 a, fp128 b, double c);
fp128 fp128_muladd2c2c1(fp128 a, fp128 b, double c);

fp128 fp128_muladd111(double a, double b, double c);

fp128 fp128_neg(fp128 a);

bool fp128_eq(fp128 a, fp128 b);
bool fp128_le(fp128 a, fp128 b);
bool fp128_ge(fp128 a, fp128 b);
bool fp128_lt(fp128 a, fp128 b);
bool fp128_gt(fp128 a, fp128 b);
bool fp128_ne(fp128 a, fp128 b);

fp128 fp128_exp(fp128 a);
fp128 fp128_pow(fp128 a, fp128 b);
fp128 fp128_sqr(fp128 a);
fp128 fp128_sqrt(fp128 a);
fp128 fp128_log(fp128 a);
fp128 fp128_log10(fp128 a);

fp128 fp128_sin(fp128 a);
fp128 fp128_cos(fp128 a);
fp128 fp128_tan(fp128 a);

fp128 fp128_asin(fp128 a);
fp128 fp128_acos(fp128 a);
fp128 fp128_atan(fp128 a);

fp128 fp128_fabs(fp128 a);

#endif

