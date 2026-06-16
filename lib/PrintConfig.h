// @Author: Guocheng - NUDT

#ifndef LLVM_PRINT_CONFIG_H
#define LLVM_PRINT_CONFIG_H

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

class DIFile;

class PrintConfig : public PassInfoMixin<PrintConfig> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void findLocalVariables(Function &F);

    void findGlobalVariables(Module &M);

    void dumpStructDecl(DICompositeType* structType, std::string structName);

    void findFunctionCalls(Function &F);

    void parseSkipLists();

    bool needScanFile(DIFile *file);

    bool needScanFunction(Function &F);

    vector<string> skipFiles;

    vector<string> skipFuncs;
    
    set<string> globalVariables;

    DenseSet<Instruction*> instToErase;

    std::map<std::string, SmallVector<std::string>> autoFPRange;  // __autofp localVars' search range

    const SmallVector<StringRef> validSearchRange = {"float", "double", "long double", "__fp16"};  // for __autofp & pragma
    
    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_PRINT_CONFIG_H
