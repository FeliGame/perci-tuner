#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	long double a = 1.21, b = 2.32, c = 5.12, d;
	d = a * b + c;

	printf("%Lf\n", log10l(d));
}
