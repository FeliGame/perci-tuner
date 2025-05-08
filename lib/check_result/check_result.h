// @Author: QiHaijun, Guocheng - NUDT

#ifndef CHECK_RESULT_H
#define CHECK_RESULT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>

#if defined(DD)

#include "dd_real.h"

void printf_fp106(dd_real a);
void printf_RE_fp106(dd_real a);

#endif

#define MYPI 3.1415926535897932384626433832795
#define ORIGINAL_RESULT "PERCI_TUNER_ORIGINAL_RESULT"
// 输出浮点数的十六进制表示
void printf_fp16(__fp16 a);
void printf_fp32(float a);
void printf_fp64(double a);
void printf_fp128(long double a);
// 判断当前值和环境变量的相对误差
void printf_RE_fp16(__fp16 a);
void printf_RE_fp32(float a);
void printf_RE_fp64(double a);
void printf_RE_fp128(long double a);

#endif
