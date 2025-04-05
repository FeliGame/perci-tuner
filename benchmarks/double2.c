#include <stdio.h>

int a = 1;
double2 dd;

int main()
{
	double2 dd1, dd2 = 1;
	double2 dd3 = dd1 + dd2 * dd1;
	printf("%ld", dd3);
}
