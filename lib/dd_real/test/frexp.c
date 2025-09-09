#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	int i;
	long double a=5, b;
	b = frexpl(a, &i);

	printf("%Lf, %Lf, %d\n", a, b, i);
}
