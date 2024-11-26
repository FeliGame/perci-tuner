#include <stdio.h>

int foo1()
{
    return 3.0;
}

int foo()
{
    float a = 3.1, b = 0.4;
    return a + b + foo1();
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