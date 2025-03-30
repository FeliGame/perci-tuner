#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "snippet.h"


struct MY_STRUCT {
    double s1;
    float s2[2];
    int *p;
} global_struct;



int comp_test(int a, int b)
{
    struct MY_STRUCT local_struct;
    struct MY_STRUCT* p = &local_struct;
    struct MY_STRUCT** pp = &p;

    __fp16 fp1 = (__fp16)1.0f, fp2 = (__fp16)2.0f;
    // 直接使用汇编指令避免发生类型转换
    fp1 += (fp2 * fp1 + fp2);
    float f1 = 1.0f, f2 = 2.0f;
    f1 += (f2 * f1 + f2);
    // asm volatile("fadd %h0, %h1, %h2"
    //             : "=w"(fp1)
    //             : "w"(fp1), "w"(fp2));
    // Apple M1 不支持bf16
    //     __bf16 bf1 = (__bf16)1.0f, bf2 = (__bf16)2.0f;
    // bf1 = bf1 + bf2;
    // asm volatile("fadd %h0, %h1, %h2"
    //             : "=w"(bf1)
    //             : "w"(bf1), "w"(bf2));

    return a - b + global_struct.s2[0] * (long double)p->s1;
}

static double fp_test()
{
    long double a = 3.1f, b = 2L + 1e-20L, c = 2L, d = 1.0f, e = 0.3f;
    double arr[2] = {0.1, 0.3};
    static double s_arr[2][3];
fmod(a, arr[0]);
    // double add = a + b;
    // double mul = a * b;
    // double div = a / b;
    // double sub = a - b;
    // double muladd = a * b + c[0];
    // double add2 = a + b + ld;

// double e = comp_test(a, b) / c[0];
    return  (b == c ? a : 0) + sin(d) * sin(e);
}

double fun()
{
    int i = 10;
    return fp_test() - i;
}

int main()
{
    const float m = 32.0;
    printf("%lf\n", fun());
    // return 0;
}