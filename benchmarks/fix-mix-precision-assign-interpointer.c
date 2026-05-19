// @Author: Guocheng
// @Description: Test fix: mix-precision assign between pointers, e.g. A[i] = B[j]
// @TEST: 
// -- sh scripts/run.sh generate-config benchmarks/fix-mix-precision-assign-interpointer.c
// -- COPY [config.json] AND MODIFY [p] [a] TO [double], THEN RUN:
// -- sh scripts/run.sh change-by-config benchmarks/fix-mix-precision-assign-interpointer.c -target=new_config.json

#include <stdlib.h>
long double* A;
int main() {
    unsigned SIZE = 3;
    long double* p = (long double*)malloc(SIZE * sizeof(long double));
    long double* q = (long double*)malloc(SIZE * sizeof(long double));
    for (unsigned i = 0; i < SIZE; ++i) {
        p[i] = 2.0L + 1e-20L;
        q[i] = 2.0L;
        p[i] = q[i];
        p[i] = p[i] * q[i];
    }
    A = (long double*)malloc(SIZE * sizeof(long double));
    for (unsigned i = 0; i < SIZE; ++i) {
        A[i] = p[i] + q[i];
        p[i] = A[i];
    }
}
