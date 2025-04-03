// @Author: Guocheng - NUDT
// Usage: Tell declared variables' type by #pragma llvm.precision.* in src, then remove these.
// Output: config.json, result_error.json

#include "SearchByPragma.h"
#include "utils/ParseTypeName.hpp"
#include "utils/ParseMetaDataNode.hpp"

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


cl::opt<string> PragmaConfigFileName("pragma-config-file", cl::value_desc("filename"), cl::desc("Config file from pragma"), cl::init("config.json"));


// Find Local vars and local static vars
void SearchByPragma::findLocalVariables(Function &F) {
    nlohmann::json function;
    auto funcName = demangle(F.getName());

    bool inRegion = false;
    SmallVector<StringRef> defaultPrecisionRange;
    DenseSet<StringRef> referencedVars; // referenced vars in region

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                // 先将pragma指定的内容存储，然后匹配对照
                if (auto *Callee = CI->getCalledFunction()) {
                    auto name = Callee->getName();
                    if (name == "llvm.precision.region.start") {
                        inRegion = true;
                        errs() << "Found region: " << F.getName() << "\n";
                    } else if (name == "llvm.precision.region.end") {
                        inRegion = false;
                        errs() << "End region: " << F.getName() << "\n";
                    } else if (inRegion && name == "llvm.precision.range") {
                        // only analyze precision range in region
                        if (auto *MD = getMetadataNode(CI, 0)) {
                            auto precisionRange = extractStringsFromMD(MD);

                            // 检查第一个元素是否在合法类型列表中，不在则将其视为变量名
                            if (std::find(validTypes.begin(), validTypes.end(), precisionRange[0]) != validTypes.end()) {
                                defaultPrecisionRange = precisionRange;
                            } 
                            else if (precisionRange.size() > 1) {
                                StringRef key = precisionRange[0];
                                for (auto &item : function) {
                                    // 找到varName对应的config项
                                    if (item["name"] == key.str()) {
                                        for (size_t i = 1; i < precisionRange.size(); ++i) {
                                            item["range"].push_back(precisionRange[i].str());
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    } else if (name == "llvm.precision.error") {
                        auto *errorMD = getMetadataNode(CI, 0);
                        auto errors = extractStringsFromMD(errorMD);
                        config["result_error"] = errors[0];
                    }
                }
            }

            // Collect referenced vars
            if (inRegion) {
                // 递归收集函数
                std::function<void(Value*)> collectValues = [&](Value *V) {
                    if (auto *LI = dyn_cast<LoadInst>(V)) {
                        errs() << "Found var from LoadInst: " << LI->getPointerOperand()->getName() << "\n";
                        referencedVars.insert(LI->getPointerOperand()->getName());
                    }
                    else if (auto *SI = dyn_cast<StoreInst>(V)) {
                        errs() << "Found var from StoreInst: " << SI->getPointerOperand()->getName() << "\n";
                        referencedVars.insert(SI->getPointerOperand()->getName());
                    }
                    // 涵盖了BinOp、UnaOp、FCmp、CallInst等所有其他指令返回结果情形
                    else if (auto *I = dyn_cast<Instruction>(V)) {
                        for (auto *User : I->users()) {
                            collectValues(User);
                        }
                    }
                };

                // 处理指令的操作数
                for (auto &Op : I.operands()) {
                    collectValues(Op);
                }
                // 处理指令的返回值本身
                collectValues(&I);
            }

            std::string varName = "";
            std::string typeName = "";
            DebugLoc DL;
            bool isStatic;

            if (auto *DDI = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DDI->getVariable())) {
                    varName = DV->getName();
                    typeName = parseDITypeName(DV->getType());
                    
                    if (varName == "this") continue;
                    DL = DDI->getDebugLoc();
                    isStatic = false;
                }
            }
            // Skip integer type
            if (typeName.find("short") != std::string::npos) continue;
            if (typeName.find("int") != std::string::npos) continue;
            if (typeName.find("long") != std::string::npos && typeName.find("long double") == std::string::npos) continue;
            if (typeName.find("long long") != std::string::npos) continue;
            if (typeName.find("signed") != std::string::npos) continue;
            if (typeName.find("unsigned") != std::string::npos) continue;
                    
            // Save floating-type vars to config
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
                
                function.emplace_back(localVar); 
            }
        }
    }
    
    nlohmann::json newFunction;
    for (auto &item : function) {
        auto varName = item["name"].get<string>();
        // If search range not specified, use default range
        if (item["range"].empty()) {
            item["range"] = defaultPrecisionRange;
        }
        // Only save referenced vars into config
        if (referencedVars.count(varName) > 0) {
            newFunction.push_back(item);
        }
    }

    config["local"][funcName] = newFunction;
}

void SearchByPragma::findGlobalVariables(Module &M) { 
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

bool SearchByPragma::runOnFunction(Function &F) {
    findLocalVariables(F);
    return false;
}

bool SearchByPragma::runOnModule(Module &module) {
    findGlobalVariables(module);

    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration()) {
            runOnFunction(*f);
        }
        
        std::error_code errorInfo; 
        raw_fd_ostream outfile(PragmaConfigFileName.c_str(), errorInfo);
        outfile << config.dump(4);  // 4 space = 1 indent
    }

    // Remove all llvm.precision.* functions to avoid backend crash
    std::vector<CallInst*> instToErase;
    for (auto &F : module) {
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
    auto &funcList = module.getFunctionList();
    auto it = funcList.begin();
    while (it != funcList.end()) {
        if (it->getName().startswith("llvm.precision")) {
            it = funcList.erase(it);
            removedDecl = true;
        } else {
            ++it;
        }
    }

    return !instToErase.empty() || removedDecl;  // hasn't changed IR
}  

void SearchByPragma::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses SearchByPragma::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running SearchByPragma Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
