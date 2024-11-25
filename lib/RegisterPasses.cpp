// @Author: Guocheng - NUDT
// Register all pass plugins

// Ref: llvm-project/llvm/Examples/Bye.cpp


#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "MyPass.h"

using namespace llvm;

/* New PM Registration */
llvm::PassPluginLibraryInfo getMyPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "PerciTuner", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // PB.registerVectorizerStartEPCallback(
            //     [](llvm::ModulePassManager &PM, OptimizationLevel Level) {
            //       PM.addPass(MyPass());
            //     });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, llvm::ModulePassManager &PM,
                   ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "mypass") {
                    PM.addPass(MyPass());
                    return true;
                  }
                  return false;
                });
          }};
}

#ifndef LLVM_MYPASS_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getMyPassPluginInfo();
}
#endif