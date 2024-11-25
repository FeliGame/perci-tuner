#ifndef LLVM_MYPASS_MYPASS_H
#define LLVM_MYPASS_MYPASS_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

namespace llvm {

class MyPass : public PassInfoMixin<MyPass> {
public:
    // static char ID;
    // MyPass() : FunctionPass(ID) {}

    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
};

} // namespace llvm

#endif // LLVM_MYPASS_MYPASS_H