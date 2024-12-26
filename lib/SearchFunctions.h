// @Author: Guocheng - NUDT

#ifndef LLVM_CREATE_CALL_DEPENDENCY_H
#define LLVM_CREATE_CALL_DEPENDENCY_H

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

namespace llvm {


class SearchFunctions : public PassInfoMixin<SearchFunctions> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void printCalledFunction(Function* callee, int depth);

    void writeToFile();
    
    void collectCalledFunctions(Function &F, std::set<Function*> &visited, int depth);

    std::set<std::string> calledFunctions;
};

} // namespace llvm

#endif // LLVM_CREATE_CALL_DEPENDENCY_H