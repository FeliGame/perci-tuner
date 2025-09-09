#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
	int i;
	long double a = 3.0e4000L;
	long double b = 4.0e4000L;
	printf("%Le, %Le, %Le, %Le\n", a, b, hypotl(a, b), sqrtl(powl(a, 2) + powl(b, 2)));
}
