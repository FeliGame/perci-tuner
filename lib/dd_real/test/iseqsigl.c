#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	int i;
	long double a = 1.0L;
	long double b = 1.0L + __LDBL_EPSILON__;
	printf("%Le, %Le, %d, %d\n", a, b, __iseqsigl(a, b), (a == b));
}
