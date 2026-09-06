// RUN: mkdir -p %t && cd %t && sh %PERCI_TUNER/scripts/run.sh print-config %s
// RUN: FileCheck %s --input-file=%t/config.json

// CHECK: "type": "sinf"

// RUN: sed 's/"type": "sinf"/"type": "sin"/' %t/config.json > %t/changed.json
// RUN: cd %t && sh %PERCI_TUNER/scripts/run.sh change-by-config -target=changed.json
// RUN: FileCheck %s --input-file=%t/changed_output.bc --check-prefix=CHECK-TRANSFORM

// CHECK-TRANSFORM: call double @sin(double

#include <math.h>

int main() {
    sinf(0.0);
    return 0;
}