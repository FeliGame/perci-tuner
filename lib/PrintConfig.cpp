// @Author: Guocheng - NUDT
// Usage: Search declared variables' type.
// --mxp: if true, only search __autofp vars or vars in #pragma region.
// Output: config.json

#include "PrintConfig.h"
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


cl::opt<bool> EnableMXP("mxp", cl::value_desc("enable pragma & autofp"), cl::desc("true or false"), cl::init(false));


// Find Local vars and local static vars
void PrintConfig::findLocalVariables(Function &F) {
    nlohmann::json function;
    auto funcName = demangle(F.getName());

    bool inRegion = false;
    SmallVector<StringRef> defaultPrecisionRange;
    std::set<std::string> searchableVars; // if EnableMXP=true, only __autofp vars & vars in pragma region searchable
    string fileName = "";
    bool retAdded = false;  // 确保每个函数只添加一个返回值配置

    for (auto &BB : F) {
        for (auto &I : BB) {
            std::string varName = "";
            std::string typeName = "";
            DebugLoc DL;
            unsigned lineNo = 0, colNo = 0;
            bool isStatic = false, isParam = false;
            
            // 处理ret指令以识别返回值
            if (auto *RI = dyn_cast<ReturnInst>(&I)) {
                // 如果返回指令有操作数（即返回值）
                if (RI->getNumOperands() > 0) {
                    Value *retVal = RI->getOperand(0);
                    varName = "ret.";
                    typeName = parseTypeName(retVal->getType());
                    // 由于返回值没有DebugInfo，保持DL为空
                    if (!retAdded) retAdded = true;
                    else typeName = ""; // 已经添加过返回值，跳过    
                }
            }
            // Get pointer safely
            else if (auto *SI = dyn_cast<StoreInst>(&I)) {
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
                    } else if (name == "llvm.precision.region.end") {
                        inRegion = false;
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
                        config["result_error"] = errors[1];  // errors[0] is var_name
                    }
                }
            }
            else if (auto *AI = dyn_cast<AllocaInst>(&I)) {
                // 适用flang-new + LLVM19后
                if (auto *N = AI->getMetadata("dbg")) {
                    if (auto *Loc = dyn_cast<DILocation>(N)) {
                        lineNo = Loc->getLine();
                        colNo = Loc->getColumn();
                        if (auto *Scope = Loc->getScope()) {
                            fileName = Scope->getFilename().str();
                        }
                    }
                    // TODO: flang-new无法输出var name?
                    if (AI->hasName()) varName = AI->getName().str();
                    typeName = parseTypeName(AI->getAllocatedType());
                }
            }

            // Collect referenced vars from IR insts
            if (inRegion) {
                std::function<void(Value*)> collectValues = [&](Value *V) {
                    if (auto *LI = dyn_cast<LoadInst>(V)) {
                        searchableVars.insert(LI->getPointerOperand()->getName().str());
                    }
                    else if (auto *SI = dyn_cast<StoreInst>(V)) {
                        searchableVars.insert(SI->getPointerOperand()->getName().str());
                    }
                    else if (auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
                        searchableVars.insert(GEP->getPointerOperand()->getName().str());
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
            if (typeName.find("integer") != std::string::npos) continue;
            if (typeName.find("size_t") != std::string::npos) continue;
            if (typeName.find("__intptr_t") != std::string::npos) continue;
                    
            // Save to config
            if (typeName != "") {
                if (DL) {  // clang读取dbg.declare调用；flang-new不支持
                    lineNo = DL.getLine();
                    colNo = DL.getCol();
                    auto *localScope = llvm::dyn_cast<llvm::DILocalScope>(DL.getScope());
                    fileName = localScope->getNonLexicalBlockFileScope()->getFile()->getFilename().str();
                }
                nlohmann::json localVar;
                localVar["name"] = varName;
                localVar["type"] = typeName;
                localVar["line"] = lineNo;
                localVar["column"] = colNo;
                localVar["file"] = fileName;
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

    config["local"][funcName] = (EnableMXP ? newFunction : function);
    autoFPRange.clear();
}

void PrintConfig::findGlobalVariables(Module &M) { 
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
                    globalVar["line"] = lineNo;
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
            if (EnableMXP)
                globalVars.emplace_back(globalVar);  // only __autofp globalVar available
        }
        if (!EnableMXP)
            globalVars.emplace_back(globalVar);
    }
    config["global"] = globalVars;
}

void PrintConfig::findStructDeclarations(Module &module) {
    nlohmann::json structDecls;

    for (auto *sty : module.getIdentifiedStructTypes()) {
        if (!sty || sty->getName().empty()) continue;
        nlohmann::json structDecl;
        unsigned i = 0;
        for (auto member : sty->elements()) {
            nlohmann::json memberInfo;
            auto typeName = parseTypeName(member);
            memberInfo["member_index"] = i++;
            memberInfo["type"] = typeName;  // We didn't need to skip integer here
            structDecl.push_back(memberInfo);
        }
        // Remove 'struct.' prefix
        structDecls[sty->getName().substr(7)] = structDecl;
    }

    config["structs"] = structDecls;
}

void PrintConfig::findFunctionCalls(Function &F) {
    nlohmann::json funcInfo;
    bool inRegion = false;
    
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (Function *Callee = CI->getCalledFunction()) {
                    auto name = Callee->getName();
                    if (name == "llvm.precision.region.start") {
                        inRegion = true;
                        continue;
                    } 
                    if (name == "llvm.precision.region.end") {
                        inRegion = false;
                        continue;
                    }
                }
                
                if (!EnableMXP || inRegion) {
                    auto *callee = CI->getCalledFunction();
                    if (callee && !callee->isIntrinsic()) {
                        auto loc = CI->getDebugLoc();
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
    }
    config["calls"][demangle(F.getName())] = funcInfo;
}

bool PrintConfig::runOnFunction(Function &F) {
    findLocalVariables(F);
    findFunctionCalls(F);
    return false;
}

bool PrintConfig::runOnModule(Module &module) {
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

    return false;  // hasn't changed IR
}  

void PrintConfig::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();  // hasn't changed IR
}

PreservedAnalyses PrintConfig::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running PrintConfig Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
