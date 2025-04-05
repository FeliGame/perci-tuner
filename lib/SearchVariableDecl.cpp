// @Author: Guocheng - NUDT
// Usage: Search declared variables' type.
// Input: 
// config.json (calls)
// --search-all: if false, only search __autofp vars or vars in #pragma region.
// Output: config.json (calls + vars)

#include "SearchVariableDecl.h"
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

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<bool> SearchAll("search-all-vars", cl::value_desc("search all variable"), cl::desc("true or flase"), cl::init(true));


// Find Local vars and local static vars
void SearchVariableDecl::findLocalVariables(Function &F) {
    nlohmann::json function;
    auto funcName = demangle(F.getName());

    bool inRegion = false;
    SmallVector<StringRef> defaultPrecisionRange;
    std::set<std::string> searchableVars; // SearchAll=false, only __autofp vars & vars in pragma region searchable

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
            else if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (auto *DDI = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                    if (auto *DV = llvm::dyn_cast<llvm::DILocalVariable>(DDI->getVariable())) {
                        varName = DV->getName();
                        typeName = parseDITypeName(DV->getType());
                        
                        if (varName == "this") continue;
                        DL = DDI->getDebugLoc();
                        isStatic = false;
                        isParam = DV->isParameter();
                    }
                }
                // Handle __autofp intrinsic
                else if (Function *Callee = CI->getCalledFunction()) {
                    auto name = Callee->getName();
                    if (name == "llvm.precision.region.start") {
                        inRegion = true;
                        errs() << "Found region: " << F.getName() << "\n";
                    } else if (name == "llvm.precision.region.end") {
                        inRegion = false;
                        errs() << "End region: " << F.getName() << "\n";
                    } else if (name == "llvm.precision.range") {
                        if (auto *MD = getMetadataNode(CI, 0)) {
                            auto precisionRange = extractStringsFromMD(MD);
                            // 如果是(float, double)形式，则一定是在region内声明的通用搜索范围
                            if (inRegion && (std::find(validSearchRange.begin(), validSearchRange.end(), precisionRange[0]) != validSearchRange.end())) {
                                defaultPrecisionRange = precisionRange;
                            } 
                            else if (precisionRange.size() > 1) {
                                if (inRegion) {
                                    for (auto &localVar : function) {
                                        // 找到varName对应的config项
                                        if (localVar["name"] == precisionRange[0].str()) {
                                            for (size_t i = 1; i < precisionRange.size(); ++i) {
                                                localVar["range"].push_back(precisionRange[i].str());
                                            }
                                            break;
                                        }
                                    }
                                } else {
                                    // 保存精度范围到临时map中
                                    autoFPRange[precisionRange[0].str()] = 
                                        SmallVector<std::string>(precisionRange.begin()+1, precisionRange.end());
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

            // Collect referenced vars from IR insts
            if (inRegion) {
                std::function<void(Value*)> collectValues = [&](Value *V) {
                    if (auto *LI = dyn_cast<LoadInst>(V)) {
                        errs() << "Found var from LoadInst: " << LI->getPointerOperand()->getName() << "\n";
                        searchableVars.insert(LI->getPointerOperand()->getName().str());
                    }
                    else if (auto *SI = dyn_cast<StoreInst>(V)) {
                        errs() << "Found var from StoreInst: " << SI->getPointerOperand()->getName() << "\n";
                        searchableVars.insert(SI->getPointerOperand()->getName().str());
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
                // Handle __autofp
                if (autoFPRange.count(varName)) {
                    localVar["range"] = autoFPRange[varName];
                    searchableVars.insert(varName);
                }
            
                function.emplace_back(localVar); 
            }
        }
    }

    nlohmann::json newFunction;
    for (auto &localVar : function) {
        auto varName = localVar["name"].get<string>();
        // If search range not specified, use default range
        if (localVar["range"].empty()) {
            localVar["range"] = defaultPrecisionRange;
        }
        // Only save referenced vars into config
        if (searchableVars.count(varName) > 0) {
            newFunction.push_back(localVar);
        }
    }

    config["local"][funcName] = (SearchAll ? function : newFunction);
    autoFPRange.clear();
}

void SearchVariableDecl::findGlobalVariables(Module &M) { 
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

        // handle __autofp
        if (MDNode *MD = GV.getMetadata("precision_range")) {
            SmallVector<string> precisionRange;
            for (const MDOperand &MDOp : drop_begin(MD->operands(), 1)) {
                if (auto *MDStr = dyn_cast<MDString>(MDOp.get())) {
                    precisionRange.push_back(MDStr->getString().str());
                }
            }
            globalVar["range"] = precisionRange;
            if (!SearchAll)
                globalVars.emplace_back(globalVar);  // only __autofp globalVar available
        }
        if (SearchAll)
            globalVars.emplace_back(globalVar);
    }
    config["global"] = globalVars;
}

void SearchVariableDecl::findStructDeclarations(Module &module) {
    nlohmann::json structDecls;

    for (auto *sty : module.getIdentifiedStructTypes()) {
        if (!sty || sty->getName().empty()) continue;
        nlohmann::json structDecl;
        unsigned i = 0;
        for (auto member : sty->elements()) {
            nlohmann::json memberInfo;
            memberInfo["member_index"] = i++;
            memberInfo["type"] = parseTypeName(member);
            structDecl.push_back(memberInfo);
        }
        // Remove 'struct.' prefix
        structDecls[sty->getName().substr(7)] = structDecl;
    }

    config["structs"] = structDecls;
}

bool SearchVariableDecl::runOnFunction(Function &F) {
    findLocalVariables(F);
    return false;
}

bool SearchVariableDecl::runOnModule(Module &module) {
    ifstream inFile("config.json");
    string name;

    if (!inFile) {
        errs() << "Unable to open config.json\n";
        exit(1);
    }

    inFile >> config;

    findGlobalVariables(module);
    findStructDeclarations(module);

    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration()) {
            runOnFunction(*f);
        }
    }

    std::error_code errorInfo; 
    raw_fd_ostream outfile("config.json", errorInfo);
    outfile << config.dump(4);  // 4 space = 1 indent

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

    return removedDecl;  // hasn't changed IR
}  

void SearchVariableDecl::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses SearchVariableDecl::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running SearchVariableDecl Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
