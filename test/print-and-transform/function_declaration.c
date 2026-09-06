// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "local"
// CHECK: "isParam": true
// CHECK: "name": "x"
// CHECK: "type": "float"
// CHECK: "name": "ret."
// CHECK: "type": "float"

// RUN: sed 's/"type": "float"/"type": "double"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM: define {{.*}} double @callee{{.*}}(double{{.*}})

// Floating-pointer parameter is not expected to be printed and transformed
// Floating-pointer parameter follow the caller's arg type.
float callee(float x, float* yp) {
    return x + *yp;
}

int main() {
    float y = 0.0;
    float* yp = &y;
    callee(3.0, yp);
    return 0;
}