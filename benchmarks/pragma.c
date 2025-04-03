// clang -S -emit-llvm pragma.c

#include <stdio.h>

int foo(double a, double b)
{
	return 2;
}

int test(int n)
{
	double x = 0, y = 0, z = 0;
// #pragma precision region
	{
// #pragma precision range(double, __fp16), x(float, __fp16), y(double, float)
		for (int i = 0; i < n; ++i)
		{
// #pragma precision abserror x(2e-6)
			y -= 1;
			z = foo(2 , 1);
		}
	}
	return (int)x;
}

int main()
{
	printf("%d", test(1000));
	return 0;
}
