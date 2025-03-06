// @Author: Guocheng - NUDT
// Usage: Tell declared variables' type.
// Input: config.json (calls)
// Output: config.json (vars)

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

#include <fstream>

using namespace std;
using namespace llvm;


// Find Local vars and local static vars
void CreatePrecisionConfig::findLocalVariables(Function &F) {
    nlohmann::json function;
    auto funcName = demangle(F.getName());

    for (auto &BB : F) {
        for (auto &I : BB) {
            std::string varName = "";
            std::string typeName = "";
            DebugLoc DL;
            bool isStatic = false, isParam = false;
            // Get pointer safely
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
                Value *Addr = SI->getPointerOperand();
                if (auto *GV = dyn_cast<GlobalVariable>(Addr)) {
                    // Check local static vars
                    if (GV->hasInternalLinkage()) {
                        // Skip global static vars
                        if (GV->getName().find(F.getName()) == std::string::npos) continue;
                        // "func_name.var_name" -> "var_name"
                        std::string prefix = F.getName().str() + ".";
                        size_t prefixLen = prefix.length();
                        varName = GV->getName().substr(prefixLen);
                        typeName = parseTypeName(GV->getValueType());

                        DL = I.getDebugLoc();
                        isStatic = true;
                    }
                }
            }
            else if (auto *DDI = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DDI->getVariable())) {
                    varName = DV->getName();
                    typeName = parseDITypeName(DV->getType());
                    
                    if (varName == "this") continue;
                    DL = DDI->getDebugLoc();
                    isStatic = false;
                    isParam = DV->isParameter();
                }
            }
            // Skip integer type
            if (typeName.find("short") != std::string::npos) continue;
            if (typeName.find("int") != std::string::npos) continue;
            if (typeName.find("long") != std::string::npos && typeName.find("long double") == std::string::npos) continue;
            if (typeName.find("long long") != std::string::npos) continue;
            if (typeName.find("signed") != std::string::npos) continue;
            if (typeName.find("unsigned") != std::string::npos) continue;
                    
            // Save to config
            if (varName != "" && typeName != "") {
                unsigned lineNo = DL.getLine();
                auto *localScope = llvm::dyn_cast<llvm::DILocalScope>(DL.getScope());
                auto *file = localScope->getNonLexicalBlockFileScope()->getFile();

                nlohmann::json localVar;
                localVar["name"] = varName;
                localVar["type"] = typeName;
                localVar["line"] = std::to_string(lineNo);
                localVar["file"] = file->getFilename().str();
                localVar["isStatic"] = isStatic;
                localVar["isParam"] = isParam;
            
                function.emplace_back(localVar); 
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
        globalVar["isStatic"] = GV.hasInternalLinkage();

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
    ifstream inFile("config.json");
    string name;

    if (!inFile) {
        errs() << "Unable to open config.json\n";
        exit(1);
    }

    inFile >> config;
    
    for (auto& [funcName, _] : config["calls"].items()) {
        includedFunctions.insert(funcName);
    }

    findGlobalVariables(module);

    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            runOnFunction(*f);
        }
    }

    std::error_code errorInfo; 
    raw_fd_ostream outfile("config.json", errorInfo);
    outfile << config.dump(4);  // 4 space = 1 indent

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
