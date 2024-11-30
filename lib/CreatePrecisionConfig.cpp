// @Author: Guocheng - NUDT
// Usage: Tell declared variables' type.
// Input: searched_functions.txt
// Output: xxx_config.json

#include "CreatePrecisionConfig.h"

#include "nlohmann/json.hpp"
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


cl::opt<string> ConfigFileName("config-file", cl::value_desc("filename"), cl::desc("Config file"), cl::init("config.json"));

set<string> includedFunctions; 
nlohmann::json config;

void CreatePrecisionConfig::findLocalVariables(Function &F) {
    nlohmann::json function;
    auto funcName = demangle(F.getName());

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *DDI = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DDI->getVariable())) {
                    nlohmann::json localVar;
                    localVar["name"] = DV->getName();
                    localVar["type"] = DV->getType()->getName();

                    function.emplace_back(localVar); 
                }
            }
            // No use in Perci-Tuner, only for DEBUG 
            else if (auto *DVI = llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
                if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DVI->getVariable())) {
                    llvm::outs() << "Local variable(val): " << DV->getName() << "\n";
                }
            }
        }
    }

    config[funcName] = function;
}

bool CreatePrecisionConfig::runOnFunction(Function &F) {
    findLocalVariables(F);
    return false;
}

bool CreatePrecisionConfig::runOnModule(Module &module) {
    ifstream inFile("searched_functions.txt");
    string name;

    if (!inFile) {
        errs() << "Unable to open searched_functions.txt\n";
        exit(1);
    }

    while(inFile >> name) {
        includedFunctions.insert(name);
    }

    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            runOnFunction(*f);
        }
        
        std::error_code errorInfo; 
        raw_fd_ostream outfile(ConfigFileName.c_str(), errorInfo);
        outfile << config.dump(4);  // 4 space = 1 indent
    }

    return false;  // hasn't changed IR
}  

void CreatePrecisionConfig::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses CreatePrecisionConfig::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running CreatePrecisionConfig Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}