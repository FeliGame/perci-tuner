// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "name": "myStruct"
// CHECK: "type": "MyStruct"
// CHECK: "name": "myStruct2"
// CHECK: "type": "MyStruct"

// RUN: sed 's/"type": "float\*"/"type": "double\*"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM: getelementptr {{.*}} %struct.MyStruct
// CHECK-TRANSFORM: store double
// CHECK-TRANSFORM-NOT: store float

struct MyStruct {
    long double l2d;
    double d2f;
    float f2h;
    __fp16 h2d;
};

typedef struct MyStruct StructType;

int main() {
    struct MyStruct myStruct;
    StructType myStruct2;
    
    myStruct.l2d = 1.0;
    myStruct2.d2f = 2.0;
    
    return 0;
}
