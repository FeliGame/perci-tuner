// @Author: Guocheng - NUDT

#ifndef LLVM_CHANGE_FUNCTION_CALL_H
#define LLVM_CHANGE_FUNCTION_CALL_H

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


class ChangeFunctionCall : public PassInfoMixin<ChangeFunctionCall> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_CHANGE_FUNCTION_CALL_H
