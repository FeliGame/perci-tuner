//===- RegisterCIRPasses.cpp - MLIR pass plugin entry point --------------===//
//
// Registers the perci-tuner CIR passes with cir-opt:
//   --load-pass-plugin=PerciTunerCIR.so --passes=print-cir-config
//   --load-pass-plugin=PerciTunerCIR.so --passes=transform-cir
//
//===----------------------------------------------------------------------===//

#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/Plugins/PassPlugin.h"

#include "CIRPrintConfig.h"
#include "CIRTransform.h"

using namespace mlir;

static void registerPerciCIRPasses() {
  PassPipelineRegistration<>(
      "print-cir-config",
      "Print floating-point variable/math-call config (config.json) from CIR",
      [](OpPassManager &pm) { pm.addPass(perci::createCIRPrintConfigPass()); });

  PassPipelineRegistration<>(
      "transform-cir",
      "Change floating-point variable and math-call precision on CIR "
      "according to config.json",
      [](OpPassManager &pm) { pm.addPass(perci::createCIRTransformPass()); });
}

extern "C" ::mlir::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
mlirGetPassPluginInfo() {
  return {MLIR_PLUGIN_API_VERSION, "PerciTunerCIR", "v0.1",
          registerPerciCIRPasses};
}
