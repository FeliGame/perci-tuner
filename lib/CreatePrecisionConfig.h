// @Author: Guocheng - NUDT

#ifndef LLVM_CREATE_PRECISION_CONFIG_H
#define LLVM_CREATE_PRECISION_CONFIG_H

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

#include <vector>

using namespace std;

namespace llvm {


class CreatePrecisionConfig : public PassInfoMixin<CreatePrecisionConfig> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void findLocalVariables(Function &F);
    
    string getTypeName(Type *type);
};

} // namespace llvm

#endif // LLVM_CREATE_PRECISION_CONFIG_H