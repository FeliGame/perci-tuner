#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    long double a = 3.1f, b = 0.4f;
    long double ld = 33.3;
    double c[2] = {0.1, 0.3};
    static double arr[2][3];

    // double add = a + b;
    // double mul = a * b;
    // double div = a / b;
    // double sub = a - b;
    // double muladd = a * b + c[0];
    // double add2 = a + b + ld;

// double e = comp_test(a, b) / c[0];
    return a > b ? a + b : -b;
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