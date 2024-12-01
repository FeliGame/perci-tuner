#include <stdio.h>
#include "snippet.hpp"

struct MY_STRUCT {
    double s1 = 0.33;
    float s2[2] = {0.4, 1.3};
    int *p;
};

MY_STRUCT global_struct;

Snippet s = Snippet();

inline int comp_test(int a, int b)
{
    Snippet local_snippet;
    MY_STRUCT local_struct;
    auto p = &local_struct;
    auto pp = &p;
    return a - b + global_struct.s2[0] - p->s1 - local_snippet.foo1(1);
}

static int fp_test()
{
    auto a = 3.1f, b = 0.4f;
    long double ld = 33.3;
    double c[2] = {0.1, 0.3};

    return a + b + comp_test(a, b) + s.foo1(a, b) + c[0];
}

int fun()
{
    float a = 3, b = 0.4 + fp_test();
    return a * b + fp_test() + s.foo1(a);
}

int main()
{
    printf("Hello World!");
    printf("%d", fun());
    return 0;
}
