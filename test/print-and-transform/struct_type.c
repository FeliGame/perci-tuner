// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "name": "myStruct"
// CHECK: "type": "MyStruct"
// CHECK: "name": "myStruct2"
// CHECK: "type": "MyStruct"

struct MyStruct {
    long double l2d ;
    double d2f;
    float f2h;
    __fp16 h2d;
};

typedef struct MyStruct StructType;

int main() {
    struct MyStruct myStruct;
    StructType myStruct2;
    return 0;
}