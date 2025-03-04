// @Author: Guocheng - NUDT
// Usage: Tell the function CallInst & their location.
// Input(Optional): "--search-entry=main",
//                  "--exclude-functions=excluded_functions.txt"
// Output: function_call.json

#include "SearchFunctionCall.h"

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
cl::opt<string> ExcludeFunctionsName("exclude-functions", cl::value_desc("name"), cl::desc("Name of functions to be excluded"), cl::init("excluded_functions.txt"));


// Print to command line, only for DEBUG!
void SearchFunctionCall::printCalledFunction(Function* callee, int depth) {
    for (int i = 0; i < depth; ++i)
    {
        errs() << "\t";
    }
    errs() << llvm::demangle(callee->getName()) << "\n";
}

// Visit function calls recursively
void SearchFunctionCall::collectCalledFunctions(Function &F, int depth) {
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *callee = call->getCalledFunction();
                if (callee && !callee->isIntrinsic()) { // LLVM intrinsic: such as llvm.fmuladd.f32
                    DebugLoc loc = call->getDebugLoc();
                    nlohmann::json callInfo;

                    callInfo["caller"] = F.getName().str();
                    callInfo["callee"] = callee->getName().str();
                    if (loc) {
                        callInfo["file"] = loc.get()->getFilename().str();
                        callInfo["line"] = loc.get()->getLine();
                        callInfo["column"] = loc.getCol();
                    }
                    config.push_back(callInfo);
                    
                    printCalledFunction(callee, depth);
                    collectCalledFunctions(*callee, depth + 1);  // DFS
                }
            }
        }
    }
}

// Write call dependency set into file
void SearchFunctionCall::writeToFile() {
    // Parse excluded functions file
    if (!ExcludeFunctionsName.empty()) {
        ifstream excludedFunctionsFile(ExcludeFunctionsName);
        if (excludedFunctionsFile.is_open()) {
            string functionName;
            while (getline(excludedFunctionsFile, functionName)) {
                excludedFunctions.insert(functionName);
            }
            excludedFunctionsFile.close();
        } else {
            errs() << "Warning: Unable to open excluded functions file: " << ExcludeFunctionsName << ", ignored\n";
        }
    }
    
    ofstream jsonFile("function_call.json");
    if (jsonFile.is_open()) {
        jsonFile << config.dump(4);
        jsonFile.close();
    }
}

bool SearchFunctionCall::runOnFunction(Function &F) {
    if (demangle(F.getName()) != CallEntryName) {
        return false;
    }

    errs() << "Call Dependency Tree:\n" << demangle(F.getName()) << "\n";
    
    nlohmann::json entryInfo;
    entryInfo["callee"] = F.getName().str();
    entryInfo["caller"] = "PROGRAM_ENTRY";
    if (auto *SP = F.getSubprogram()) {
        entryInfo["file"] = SP->getFilename().str();
        entryInfo["line"] = SP->getLine();
    }
    config.push_back(entryInfo);

    collectCalledFunctions(F, 1);
    writeToFile();

    return false;
}

bool SearchFunctionCall::runOnModule(Module &module) {
    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        runOnFunction(*f);
    }
    return false;  // hasn't changed IR
}  

void SearchFunctionCall::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses SearchFunctionCall::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running SearchFunctionCall Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}