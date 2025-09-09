#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	long double a=5, b=3, c;
	c = modfl(a, &b);

	printf("%Lf, %Lf, %Lf\n", a, b, c);
}
