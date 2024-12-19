// @Author: Guocheng - NUDT
// Usage: Tell declared variables' type.
// Input: searched_functions.txt
// Output: xxx_config.json

#include "CreatePrecisionConfig.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/BinaryFormat/Dwarf.h>

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<string> ConfigFileName("config-file", cl::value_desc("filename"), cl::desc("Config file"), cl::init("config.json"));

void CreatePrecisionConfig::findLocalVariables(Function &F) {
    nlohmann::json function;
    auto funcName = demangle(F.getName());

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *DDI = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DDI->getVariable())) {
                    auto varName = DV->getName();
                    auto typeName = parseDITypeName(DV->getType());
                    
                    if (varName == "this") continue;
                    if (typeName == "int") continue;
                    if (typeName == "long") continue;
                    if (typeName == "long long") continue;
                    
                    auto DL = DDI->getDebugLoc();
                    unsigned lineNo = DL.getLine();
                    auto *localScope = llvm::dyn_cast<llvm::DILocalScope>(DL.getScope());
                    auto *file = localScope->getNonLexicalBlockFileScope()->getFile();

                    nlohmann::json localVar;
                    localVar["name"] = varName;
                    localVar["type"] = typeName;
                    localVar["line"] = std::to_string(lineNo);
                    localVar["file"] = file->getFilename().str();
                
                    function.emplace_back(localVar); 
                }
            }
            // No use in Perci-Tuner, only for DEBUG 
            else if (auto *DVI = llvm::dyn_cast<llvm::DbgValueInst>(&I)) {
                if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DVI->getVariable())) {
                    errs() << "Local variable(val): " << DV->getName() << "\n";
                }
            }
        }
    }

    config["local"][funcName] = function;
}

void CreatePrecisionConfig::findGlobalVariables(Module &M) { 
    nlohmann::json globalVars;

    for (GlobalVariable &GV : M.globals()) {
        if (GV.isDeclaration()) continue;

        auto name = demangle(GV.getName());
        // Skip vars not declared by user. 
        if(name.find('.') != string::npos) continue;

        nlohmann::json globalVar;
        globalVar["name"] = name;
        globalVar["type"] = parseTypeName(GV.getValueType());

        // 获取与全局变量关联的调试信息
        SmallVector<DIGlobalVariableExpression *, 1> GVs;
        GV.getDebugInfo(GVs);
        if (!GVs.empty()) {
            DIGlobalVariableExpression *GVE = GVs[0];
            if (GVE) {
                DIGlobalVariable *GVDebug = GVE->getVariable();
                if (GVDebug) {
                    unsigned lineNo = GVDebug->getLine();
                    DIFile *file = GVDebug->getFile();
                    globalVar["line"] = std::to_string(lineNo);
                    globalVar["file"] = file->getFilename().str();
                }
            }
        }

        globalVars.emplace_back(globalVar);
    }
    config["global"] = globalVars;
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

    findGlobalVariables(module);

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
