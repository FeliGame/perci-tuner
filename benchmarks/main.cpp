#include <stdio.h>
#include "snippet.hpp"
#include <cstdlib>
#include <string>
#include <iostream>

using namespace std;

struct MY_STRUCT {
    double s1 = 0.33;
    float s2[2] = {0.4, 1.3};
    int *p;
};

MY_STRUCT global_struct;

Snippet s = Snippet();
double d = 32.0;
string g_str = "hello";

inline int comp_test(int a, int b)
{
    Snippet local_snippet;
    MY_STRUCT local_struct;
    auto p = &local_struct;
    auto snippet = new Snippet();
    auto pp = &p;
    return a - b + global_struct.s2[0] - p->s1 - local_snippet.foo1(1);
}

static int fp_test()
{
    auto a = 3.1f, b = 0.4f;
    long double ld = 33.3;
    double c[2] = {0.1, 0.3};
    double arr[2][3];

    return a + b + comp_test(a, b) + s.foo1(a, b) + c[0];
}

int fun()
{
    float a = 3, c = 32;
    int i = 10;
    return a * c + fp_test() + s.foo1(a) - i;
}

int main()
{
    const float m = 32.0;
    const char* str = "main without brackets??";
    // Issue: put here cancel CCD func search
    // string l_str = "local...";
    printf("%s", str);
    printf("%d", fun());
    return 0;
}
