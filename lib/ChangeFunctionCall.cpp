// @Author: Guocheng - NUDT
// Usage: Change CallInst if necessary.
// Input: function_call.json
// Output: CallInst-changed IR

#include "ChangeFunctionCall.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

#include <fstream>

using namespace std;
using namespace llvm;

bool ChangeFunctionCall::runOnFunction(Function &F) {
    bool modified = false;
    
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                DebugLoc loc = call->getDebugLoc();
                if (!loc) continue;

                auto *oldFunc = call->getCalledFunction();
                if (!oldFunc) {
                    errs() << "No called function\n";
                    continue; // TODO: It's strange
                }
                auto oldFuncName = oldFunc->getName();
                
                // TODO: Can be more efficient
                for (auto& entry : config) {
                    // Locating the CallInst
                    if (entry["file"] == loc->getFilename().str() && 
                        entry["line"] == loc->getLine() &&
                        entry["column"] == loc->getColumn()) {
                        string targetCallee = entry["callee"].get<string>();
                        errs() << "checking: " << oldFuncName << " -> " << targetCallee << "\n";
                        
                        if (oldFuncName != targetCallee) {
                            errs() << oldFunc->getName() << " changed to: " << targetCallee << "\n";
                            FunctionCallee newFunc = F.getParent()->getOrInsertFunction(
                                targetCallee, 
                                oldFunc->getFunctionType()
                            );
                            call->setDebugLoc(loc);
                            call->setCalledFunction(newFunc);

                            modified = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    return modified;
}

bool ChangeFunctionCall::runOnModule(Module &module) {
    ifstream inFile("function_call.json");

    if (!inFile) {
        errs() << "Error: Unable to open function_call.json\n";
        exit(1);
    }

    inFile >> config;
    
    bool modified = false;
    for(auto &F: module) {
        if(!F.isDeclaration()) {
            modified |= runOnFunction(F);
        }
    }

    return modified;
}  

void ChangeFunctionCall::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll(); 
}

PreservedAnalyses ChangeFunctionCall::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running ChangeFunctionCall Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
