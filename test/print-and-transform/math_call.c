// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "type": "sinf"

#include <math.h>

int main() {
    sinf(0.0);
    return 0;
}