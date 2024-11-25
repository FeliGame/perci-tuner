#include <stdio.h>

int fun()
{
    float a = 3, b = 0.4;
    return a * b;
}

int foo()
{
    float a = 3.1, b = 0.4;
    return a + b;
}

int main()
{
    printf("Hello World!");
    printf("%d", fun());
    return 0;
}