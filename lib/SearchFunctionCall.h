// @Author: Guocheng - NUDT

#ifndef LLVM_SEARCH_FUNCTION_CALL_H
#define LLVM_SEARCH_FUNCTION_CALL_H

#include "nlohmann/json.hpp"

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

namespace llvm {


class SearchFunctionCall : public PassInfoMixin<SearchFunctionCall> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void parseExcludedFunctions();

    void writeToFile();

    std::set<std::string> excludedFunctions;

    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_SEARCH_FUNCTION_CALL_H