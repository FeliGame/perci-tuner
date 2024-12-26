// @Author: Guocheng - NUDT
// Usage: Tell the function call chain in run-time order.
// Input(Optional): "--search-entry=function(TypeList if C++)"
// Output: searched_functions.txt

#include "SearchFunctions.h"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<string> CallEntryName("search-entry", cl::value_desc("name"), cl::desc("Name of program entry"), cl::init("main"));


// Print to command line, only for DEBUG!
void SearchFunctions::printCalledFunction(Function* callee, int depth) {
    for (int i = 0; i < depth; ++i)
    {
        errs() << "\t";
    }
    errs() << llvm::demangle(callee->getName()) << "\n";
}

// Visit function calls recursively
void SearchFunctions::collectCalledFunctions(Function &F, std::set<Function*> &visited, int depth) {
    // if (!visited.insert(&F).second) {
    //     return;  // exit if visited
    // }
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *callee = call->getCalledFunction();
                if (callee && !callee->isIntrinsic()) { // LLVM intrinsic: such as llvm.fmuladd.f32
                    calledFunctions.insert(callee->getName().str());
                    printCalledFunction(callee, depth);
                    collectCalledFunctions(*callee, visited, depth + 1);
                }
            }
        }
    }
}

// Write call dependency set into file
void SearchFunctions::writeToFile() {
    ofstream searchedFunctionsFile ("searched_functions.txt");
    if (searchedFunctionsFile.is_open()) {
        for (auto &function : calledFunctions) {
            searchedFunctionsFile << function << "\n";
        }
        searchedFunctionsFile.close();
    }
}

bool SearchFunctions::runOnFunction(Function &F) {
    if (demangle(F.getName()) != CallEntryName) {
        return false;
    }

    errs() << "Call Dependency Tree:\n" << demangle(F.getName()) << "\n";
    calledFunctions.insert(F.getName().str());

    std::set<Function*> visited;
    collectCalledFunctions(F, visited, 1);

    writeToFile();

    return false;
}

bool SearchFunctions::runOnModule(Module &module) {
    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        runOnFunction(*f);
    }
    return false;  // hasn't changed IR
}  

void SearchFunctions::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses SearchFunctions::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running SearchFunctions Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}