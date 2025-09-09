#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	int i;
	long double a = 1.0/0.0;
	printf("%Lf, %d, %d\n", a, __isinfl(a), __isnanl(a));
}
