// @Author: Guocheng - NUDT

#ifndef LLVM_REMOVE_PRAGMA_H
#define LLVM_REMOVE_PRAGMA_H

#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

using namespace std;

namespace llvm {


class RemovePragma : public PassInfoMixin<RemovePragma> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
};

} // namespace llvm

#endif // LLVM_REMOVE_PRAGMA_H
