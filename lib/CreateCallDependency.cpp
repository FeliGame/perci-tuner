#include "CreateCallDependency.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

using namespace std;
using namespace llvm;


cl::opt<string> CallEntryName("search-entry", cl::value_desc("name"), cl::desc("Name of program entry"), cl::init("main"));

std::set<std::string> CalledFunctions;


void printCalledFunctions(Function* callee, int depth) {
    for (int i = 0; i < depth; ++i)
    {
        errs() << "\t";
    }
    errs() << llvm::demangle(callee->getName()) << "\n";
}

// Enter function calls recursively
void collectCalledFunctions(Function &F, std::set<Function*> &visited, int depth) {
    // if (!visited.insert(&F).second) {
    //     return;  // exit if visited
    // }
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *callee = call->getCalledFunction();
                if (callee && !callee->isIntrinsic()) { // LLVM intrinsic: such as llvm.fmuladd.f32
                    CalledFunctions.insert(callee->getName().str());
                    printCalledFunctions(callee, depth);
                    collectCalledFunctions(*callee, visited, depth + 1); // 递归调用
                }
            }
        }
    }
}

bool CreateCallDependency::runOnFunction(Function &F) {
    if (demangle(F.getName()) != CallEntryName) {
        return false;
    }

    errs() << "Call Dependency Tree:\n" << demangle(F.getName()) << "\n";
    std::set<Function*> visited;
    collectCalledFunctions(F, visited, 1);

    return false;
}

bool CreateCallDependency::runOnModule(Module &module) {
    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        runOnFunction(*f);
    }
    return false;  // hasn't changed IR
}  

void CreateCallDependency::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

PreservedAnalyses CreateCallDependency::run(Module &M, ModuleAnalysisManager &) {
    errs() << "Perci-Tuner: Running CreateCallDependency Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}