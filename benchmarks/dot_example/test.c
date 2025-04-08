#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#if defined(FP128)
#define type long double
#elif defined(D)
#define type double
#endif


double mytimer(void) {
	struct timeval tp;
	static long start=0, startu;
	if (!start) {
		gettimeofday(&tp, NULL);
		start = tp.tv_sec;
		startu = tp.tv_usec;
		return(0.0);
	}
	gettimeofday(&tp, NULL);
	return( ((double) (tp.tv_sec - start)) + (tp.tv_usec-startu)/1000000.0 );
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("%s n\n", argv[0]);
		exit(-1);
	}

	int n = atoi(argv[1]);

	type *a = (type *)malloc(n*sizeof(type));
	type *b = (type *)malloc(n*sizeof(type));

	for (int i = 0; i < n; i++) {
		a[i] = (type)cosl((double)i+1);
		b[i] = (type)sinl((double)i+1);
	}

	type res = 0.0;

	double st1 = mytimer();
	for (int j = 0; j < n; j++) {
		res += a[j] * b[j];
	}
	double et1 = mytimer();


	long double rres = res;
	printf("%Le\n", rres);

	free(a);
	free(b);

	return 0;
}
