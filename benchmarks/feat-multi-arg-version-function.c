// @Author: Guocheng
// @Description: Test INCOMPATIBLE feat: multi-version functions according to callinst's actual args' type.
// @TEST: 
// -- sh scripts/run.sh generate-config benchmarks/feat-multi-arg-version-function.c
// -- sh scripts/run.sh change-by-config benchmarks/feat-multi-arg-version-function.c -target=config.json
#include <stdio.h>
#include <stdlib.h>

long double foo(long double* arg, long double* arr, long double val) {
    *arg = (*arg) + arr[0] + arr[1] + val;
    return *arg;
}

int main() {
    long double val = 1+1e-16L;
    long double* ptr = &val;
    unsigned SIZE = 3;
    long double* arr1 = (long double*)malloc(SIZE);
    arr1[0] = 2;
    arr1[1] = 3;

    double* arr = (long double*)malloc(SIZE);
    arr[0] = 2;
    arr[1] = 3;
    double val1 = 2.01;
    
    printf("%Lf", foo(&val, arr1, val) + foo(&val, arr1, val1) + foo(&val, arr, 3.99));

    return 0;
}