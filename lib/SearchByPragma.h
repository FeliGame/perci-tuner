// @Author: Guocheng - NUDT

#ifndef LLVM_SEARCH_BY_PRAGMA_H
#define LLVM_SEARCH_BY_PRAGMA_H

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


class SearchByPragma : public PassInfoMixin<SearchByPragma> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void findLocalVariables(Function &F);

    void findGlobalVariables(Module &M);

    set<string> includedFunctions; 
    
    set<string> globalVariables;

    DenseSet<Instruction*> instToErase;

    const SmallVector<StringRef> validTypes = {"float", "double", "long double", "__fp16"};
    
    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_SEARCH_BY_PRAGMA_H
