#include <stdio.h>
#include "snippet.h"


Snippet s = Snippet();

inline int foo1(int a, int b)
{
    return a - b;
}

int foo()
{
    float a = 3.1, b = 0.4;

    return a + b + foo1(a, b) + s.foo1(a, b);
}

int fun()
{
    float a = 3, b = 0.4 + foo();
    return a * b + foo() + s.foo1(a);
}

int main()
{
    printf("Hello World!");
    printf("%d", fun());
    return 0;
}