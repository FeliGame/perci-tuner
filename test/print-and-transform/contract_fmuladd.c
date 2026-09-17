// This test checks the contract of fmuladd instruction after Perci-Tuner transform.

// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/output.bc

// CHECK-NOT: call double @llvm.fmuladd.f64
// CHECK: fmul contract
// CHECK: fadd contract

// RUN: sed 's/"type": "float"/"type": "double"/' %t/config.json > %t/changed.json
// RUN: sed 's/"type": "__fp16"/"type": "double"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM-NOT: call double @llvm.fmuladd.f64
// CHECK-TRANSFORM: fmul contract
// CHECK-TRANSFORM: fadd contract

#include <stdio.h>
int main() {
    __fp16 a;
    float b;
    double c;
    return (a * b + c) + a * a + b;
}