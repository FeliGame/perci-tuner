// @Author: Guocheng - NUDT

#ifndef LLVM_MERGE_MULTI_VERSION_H
#define LLVM_MERGE_MULTI_VERSION_H

#include "nlohmann/json.hpp"

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

#include <vector>
#include <set>

using namespace std;

namespace llvm {


class MergeMultiVersion : public PassInfoMixin<MergeMultiVersion> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    DenseSet<CallInst*> processed;  // to avoid processing the same CallInst multiple times
    
    Function* mvSelectFunction;
};

} // namespace llvm

#endif // LLVM_MERGE_MULTI_VERSION_H
