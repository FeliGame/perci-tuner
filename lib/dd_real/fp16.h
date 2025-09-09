#ifndef FP16_H
#define FP16_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

__fp16 fp16_add(__fp16 a, __fp16 b);
__fp16 fp16_sub(__fp16 a, __fp16 b);
__fp16 fp16_mul(__fp16 a, __fp16 b);
__fp16 fp16_div(__fp16 a, __fp16 b);
bool fp16_gt(__fp16 a, __fp16 b);
bool fp16_ge(__fp16 a, __fp16 b);
bool fp16_lt(__fp16 a, __fp16 b);
bool fp16_le(__fp16 a, __fp16 b);
bool fp16_eq(__fp16 a, __fp16 b);
bool fp16_ne(__fp16 a, __fp16 b);

#endif
