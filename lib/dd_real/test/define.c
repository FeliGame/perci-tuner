#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define D1(x, y, f) {	\
	y = f(x);	\
}

#define F1(x, f) {		\
	printf("%lf\n", f##in(x));	\
	return 0;		\
}

//int main() {
//	double x;
//	D1(1.23, x, sin);
//
//	printf("%lf\n", x);
//}

int main() 		F1(1.23, s)
