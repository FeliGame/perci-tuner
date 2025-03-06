// @Author: Guocheng - NUDT
// Usage: Tell the function CallInst & their location.
// Input(Optional): "--exclude-functions=excluded_functions.txt"
// Output: config.json (calls)

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


cl::opt<string> ExcludeFunctionsName("exclude-functions", cl::value_desc("name"), cl::desc("Name of functions to be excluded"), cl::init("excluded_functions.txt"));

void SearchFunctionCall::parseExcludedFunctions() {
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
}

void SearchFunctionCall::writeToFile() {
    ofstream jsonFile("config.json");
    if (jsonFile.is_open()) {
        jsonFile << config.dump(4);
        jsonFile.close();
    }
}

bool SearchFunctionCall::runOnFunction(Function &F) {
    nlohmann::json funcInfo;
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *callee = call->getCalledFunction();
                if (callee && !callee->isIntrinsic()) { // LLVM intrinsic: such as llvm.fmuladd.f32
                    auto loc = call->getDebugLoc();
                    
                    nlohmann::json callInfo;
                    callInfo["type"] = callee->getName().str();
                    if (loc) {
                        callInfo["file"] = loc->getFilename().str();
                        callInfo["line"] = loc.get()->getLine();
                        callInfo["column"] = loc.getCol();
                    }
                    funcInfo.emplace_back(callInfo);   
                }
            }
        }

    }
    auto callerName = demangle(F.getName());
    config["calls"][callerName] = funcInfo;

    return false;
}

bool SearchFunctionCall::runOnModule(Module &module) {    
    parseExcludedFunctions();
    for (auto item : excludedFunctions)
    {
        errs() << "Excluded function: " << item << "\n";
    }

    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        if(!f->isDeclaration() && excludedFunctions.find(f->getName().str()) == excludedFunctions.end())
            runOnFunction(*f);
    }

    writeToFile();
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