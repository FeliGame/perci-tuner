// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "name": "l2d"
// CHECK: "type": "long double"
// CHECK: "name": "d2f"
// CHECK: "type": "double"
// CHECK: "name": "f2h"
// CHECK: "type": "float"
// CHECK: "name": "h2d"
// CHECK: "type": "__fp16"

// RUN: sed 's/"type": "long double"/"type": "double"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM: %l2d = alloca double, align 8

int main() {
    long double l2d = 3.0;
    double d2f = 3.0;
    float f2h = 3.0f;
    __fp16 h2d = 3.0;
    return 0;
}