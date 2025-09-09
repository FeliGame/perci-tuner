#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	int i;
	long double a=5;
	printf("%Lf, %Lf, %Lf\n", a, logl(a), logbl(a));
}
