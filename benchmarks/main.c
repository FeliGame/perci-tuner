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
    return a - b + global_struct.s2[0] - p->s1;
}

static double fp_test()
{
    const long double a = 3.1f, b = 2L + 1e-20L, c = 2L, d = 1.0f, e = 0.3f;
    double arr[2] = {0.1, 0.3};
    static double s_arr[2][3];

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
    const char* str = "main.c output:";
    // printf("%s\n", str);
    printf("%lf\n", fun());
    // return 0;
}