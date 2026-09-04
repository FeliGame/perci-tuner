// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "global"

// CHECK: "isStatic": true
// CHECK: "type": "long double"
// CHECK: "isStatic": true
// CHECK: "type": "double"
// CHECK: "isStatic": true
// CHECK: "type": "float"
// CHECK: "isStatic": true
// CHECK: "type": "__fp16"

// CHECK: "isStatic": false
// CHECK: "type": "long double"
// CHECK: "isStatic": false
// CHECK: "type": "double"
// CHECK: "isStatic": false
// CHECK: "type": "float"
// CHECK: "isStatic": false
// CHECK: "type": "__fp16"

// CHECK: "local"

// CHECK: "isStatic": true
// CHECK: "type": "long double"
// CHECK: "isStatic": true
// CHECK: "type": "double"
// CHECK: "isStatic": true
// CHECK: "type": "float"
// CHECK: "isStatic": true
// CHECK: "type": "__fp16"


long double _l2d;
double _d2f;
float _f2h;
__fp16 _h2d;

static long double _sl2d;
static double _sd2f;
static float _sf2h;
static __fp16 _sh2d;

int main() {
    static long double l2d;
    static double d2f;
    static float f2h;
    static __fp16 h2d;
    // Static must be defined (not initialized) or will be omitted
    _sl2d = l2d = 1.0;
    _sd2f = d2f = 1.0;
    _sf2h = f2h = 1.0;
    _sh2d = h2d = 1.0;

    return 0;
}