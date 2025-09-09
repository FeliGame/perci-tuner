#ifndef IBM_H
#define IBM_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "dd_real.h"
#include "ldouble.h"
#include "bf16.h"

dd_real init_ppctodd(double2 a);
double2 init_ddtoppc(dd_real a);

fp128  PPCToFP128(double2 a);
double PPCToFP64(double2 a);
float  PPCToFP32(double2 a);
__fp16 PPCToFP16(double2 a);
__bf16 PPCToBF16(double2 a);

double2 FP128ToPPC(fp128 a);
double2 FP64ToPPC(double a);
double2 FP32ToPPC(float  a);
double2 FP16ToPPC(__fp16 a);
double2 BF16ToPPC(__bf16 a);

uint32_t PPCToU32(double2 a);
uint64_t PPCToU64(double2 a);
int32_t  PPCToS32(double2 a);
int64_t  PPCToS64(double2 a);

double2 U32ToPPC(uint32_t a);
double2 U64ToPPC(uint64_t a);
double2 S32ToPPC(int32_t  a);
double2 S64ToPPC(int64_t  a);

void ppc_printf(double2 a);

double2 ppc_muladd222(double2 a, double2 b, double2 c);
double2 ppc_muladd221(double2 a, double2 b, double c);
double2 ppc_muladd212(double2 a, double b, double2 c);
double2 ppc_muladd122(double a, double2 b, double2 c);
double2 ppc_muladd211(double2 a, double b, double c);
double2 ppc_muladd121(double a, double2 b, double c);
double2 ppc_muladd112(double a, double b, double2 c);

double2 ppc_add22(double2 a, double2 b);
double2 ppc_add21(double2 a, double b);
double2 ppc_add12(double a, double2 b);

double2 ppc_sub22(double2 a, double2 b);
double2 ppc_sub21(double2 a, double b);
double2 ppc_sub12(double a, double2 b);

double2 ppc_mul22(double2 a, double2 b);
double2 ppc_mul21(double2 a, double b);
double2 ppc_mul12(double a, double2 b);

double2 ppc_div22(double2 a, double2 b);
double2 ppc_div21(double2 a, double b);
double2 ppc_div12(double a, double2 b);

bool ppc_eq(double2 a, double2 b);
bool ppc_le(double2 a, double2 b);
bool ppc_ge(double2 a, double2 b);
bool ppc_lt(double2 a, double2 b);
bool ppc_gt(double2 a, double2 b);
bool ppc_ne(double2 a, double2 b);

double2 ppc_exp(double2 a);
double2 ppc_pow(double2 a, double2 b);
double2 ppc_sqr(double2 a);
double2 ppc_sqrt(double2 a);
double2 ppc_log(double2 a);
double2 ppc_log10(double2 a);

double2 ppc_sin(double2 a);
double2 ppc_cos(double2 a);
double2 ppc_tan(double2 a);

double2 ppc_asin(double2 a);
double2 ppc_acos(double2 a);
double2 ppc_atan(double2 a);

double2 ppc_fabs(double2 a);

#endif
