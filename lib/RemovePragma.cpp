// @Author: Guocheng - NUDT
// Usage: remove llvm precision pragmas to prevent compilation error.

#include "RemovePragma.h"
#include "utils/ParseTypeName.hpp"
#include "utils/ParseMetaDataNode.hpp"

#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>

using namespace std;
using namespace llvm;


void RemovePragma::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses RemovePragma::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running RemovePragma Pass...\n";
    
    // Remove all llvm.precision.* functions
    std::vector<CallInst*> instToErase;
    for (auto &F : M) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *CI = dyn_cast<CallInst>(&I)) {
                    if (Function *Callee = CI->getCalledFunction()) {
                        if (Callee->getName().startswith("llvm.precision")) {
                            instToErase.push_back(CI);
                        }
                    }
                }
            }
        }
    }
    for (auto *CI : instToErase) {
        CI->eraseFromParent();
    }

    // Remove all llvm.precision.* decl
    bool removedDecl = false;
    auto &funcList = M.getFunctionList();
    auto it = funcList.begin();
    while (it != funcList.end()) {
        if (it->getName().startswith("llvm.precision")) {
            it = funcList.erase(it);
            removedDecl = true;
        } else {
            ++it;
        }
    }

    if (!removedDecl)
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
