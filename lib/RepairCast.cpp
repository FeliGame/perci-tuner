// @Author: Guocheng - NUDT
// Usage: Repair wrong casts caused by changed precisions.
// Input: Cast-incorrect IR
// Output: Cast-correct runnable IR

#include "RepairCast.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/Verifier.h>

#include <fstream>

using namespace std;
using namespace llvm;


bool RepairCast::runOnFunction(Function &F) {
    bool modified = false;
    
    for (auto &BB : F) {
        SmallVector<CallInst*, 4> toDelete;  // Delete later to avoid break iterator
        for (auto &I : BB) {
            
        }

        for (auto *call : toDelete) {
            call->eraseFromParent();
        }
    }

    return modified;
}

bool RepairCast::runOnModule(Module &module) {
    bool modified = false;
    for(auto &F: module) {
        if(!F.isDeclaration()) {
            modified |= runOnFunction(F);
        }
    }

    // module.print(outs(), nullptr);
    if (verifyModule(module, &errs()))
    {
        errs() << "Module verify failed!\n";
        exit(1);
    } else {
        errs() << "Module verify success!\n";
    }

    return modified;
}  

void RepairCast::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll(); 
}

PreservedAnalyses RepairCast::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running RepairCast Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
