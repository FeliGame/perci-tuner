#include <stdio.h>

inline int foo1(int a, int b)
{
    return a - b;
}

int foo()
{
    float a = 3.1, b = 0.4;
    return a + b + foo1(a, b);
}

int fun()
{
    float a = 3, b = 0.4 + foo();
    return a * b + foo();
}

int main()
{
    printf("Hello World!");
    printf("%d", fun());
    return 0;
}