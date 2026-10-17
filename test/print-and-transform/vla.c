// clang yields __vla_exprN variables for VLA dimensions, which have no file/line debug info
// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "name": "a"
// CHECK: "type": "double[0]"

// RUN: sed 's/"type": "double\[0\]"/"type": "float[0]"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM: %__vla_expr0 = alloca i64
// CHECK-TRANSFORM: %vla = alloca {{.*}}float{{.*}}, i64

#include <stdio.h>

int main(int argc, char **argv) {
    int n = argc + 2;
    double a[n];  // VLA
    a[0] = 1.0;
    printf("%f %d\n", a[0], n);
    return 0;
}
