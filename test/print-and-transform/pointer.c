// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "type": "long double*"
// CHECK: "type": "double*"
// CHECK: "type": "float*"
// CHECK: "type": "__fp16*"

int main() {
    long double* l2d;
    double* d2f;
    float* f2h;
    __fp16* h2d;
    return 0;
}