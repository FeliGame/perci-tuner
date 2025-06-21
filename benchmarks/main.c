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

double pointer_test()
{
    float a_val = 3.0f;
    double b_val = 1.0;
    float* a = &a_val;
    double* b = &b_val;

    float c = *a + *b;
    return (double)(c);
}

double fun()
{
    return pointer_test();
}

int main()
{
    long double l = 2.0+1e-20L;
    if(l != 2.0+1e-20L) {
        printf("0");
        return 0;
    }
    const float m = 32.0;
    double c = l - m;
    printf("%lf\n", c);
    // return 0;
}