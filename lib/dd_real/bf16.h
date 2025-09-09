#ifndef BF16_H
#define BF16_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef fp128
#define fp128 long double
#endif

__bf16 FP32ToBF16(float a);
float BF16ToFP32(__bf16 a);
__bf16 FP64ToBF16(double a);
double BF16ToFP64(__bf16 a);
__bf16 FP128ToBF16(fp128 a);
fp128 BF16ToFP128(__bf16 a);

__bf16 bf16_add(__bf16 a, __bf16 b);
__bf16 bf16_sub(__bf16 a, __bf16 b);
__bf16 bf16_mul(__bf16 a, __bf16 b);
__bf16 bf16_div(__bf16 a, __bf16 b);

bool bf16_eq(__bf16 a, __bf16 b);
bool bf16_le(__bf16 a, __bf16 b);
bool bf16_ge(__bf16 a, __bf16 b);
bool bf16_lt(__bf16 a, __bf16 b);
bool bf16_gt(__bf16 a, __bf16 b);
bool bf16_ne(__bf16 a, __bf16 b);
//inline bool bf16_and(__bf16 a, __bf16 b);
//inline bool bf16_or(__bf16 a, __bf16 b);
//inline bool bf16_not(__bf16 a);

#endif
