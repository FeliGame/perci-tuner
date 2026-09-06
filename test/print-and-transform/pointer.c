// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "type": "long double*"
// CHECK: "type": "double*"
// CHECK: "type": "float*"
// CHECK: "type": "__fp16*"

// RUN: sed 's/"type": "float\*"/"type": "half\*"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM: getelementptr {{.*}} [2 x half]
// CHECK-TRANSFORM-NOT: getelementptr {{.*}} [2 x float]

int main() {
    long double l2d_arr[2] = {1.0, 2.0};
    double d2f_arr[2] = {1.0, 2.0};
    float f2h_arr[2] = {1.0f, 2.0f};
    __fp16 h2d_arr[2] = {1.0, 2.0};
    
    long double* l2d = l2d_arr;
    double* d2f = d2f_arr;
    float* f2h = f2h_arr;
    __fp16* h2d = h2d_arr;
    
    long double val = l2d[0];
    return 0;
}
