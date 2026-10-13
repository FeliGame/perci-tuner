// RUN: mkdir -p %t-restrict && cd %t-restrict && sh %PERCI_TUNER/scripts/run.sh search-optimize -algorithm=exhaust -search-range=64,128 -arg=2 %s -lm
// RUN: (echo BEGIN; LC_ALL=C grep -oE -- "->[^']+'" %t-restrict/run_history.csv | sed "s/^->//; s/'//g" | LC_ALL=C sort -u; echo END) > %t-restrict/newtypes.txt
// RUN: FileCheck %s --input-file=%t-restrict/newtypes.txt --check-prefix=CHECK-RANGE

// RUN: mkdir -p %t-default && cd %t-default && sh %PERCI_TUNER/scripts/run.sh search-optimize -algorithm=exhaust -arg=2 %s -lm
// RUN: (echo BEGIN; LC_ALL=C grep -oE -- "->[^']+'" %t-default/run_history.csv | sed "s/^->//; s/'//g" | LC_ALL=C sort -u; echo END) > %t-default/newtypes.txt
// RUN: FileCheck %s --input-file=%t-default/newtypes.txt --check-prefix=CHECK-DEFAULT

// RUN: mkdir -p %t-mix && cd %t-mix && sh %PERCI_TUNER/scripts/run.sh search-optimize -algorithm=exhaust -search-range=32 -arg=2 %s -lm
// RUN: tail -n +2 %t-mix/run_history.csv | grep -v calls | grep -- "long double->float" > %t-mix/mixed.txt
// RUN: FileCheck %s --input-file=%t-mix/mixed.txt --check-prefix=CHECK-MIX

// CHECK-RANGE: BEGIN
// CHECK-RANGE-NEXT: double
// CHECK-RANGE-NEXT: sqrt
// CHECK-RANGE-NEXT: END

// CHECK-DEFAULT: BEGIN
// CHECK-DEFAULT-NEXT: __fp16
// CHECK-DEFAULT-NEXT: double
// CHECK-DEFAULT-NEXT: float
// CHECK-DEFAULT-NEXT: sqrt
// CHECK-DEFAULT-NEXT: sqrtf
// CHECK-DEFAULT-NEXT: END

// CHECK-MIX: long double->float

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    long double acc = (argc > 1) ? (long double)atof(argv[1]) : 2.0L;
    acc = sqrtl(acc) + acc;
    printf("%.17Lg\n", acc);
    return 0;
}