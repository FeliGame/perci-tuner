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
            else if (auto *SI = dyn_cast<StoreInst>(&I)) {
                Value *Addr = SI->getPointerOperand();
                GlobalVariable *GV = nullptr;
            
                // 直接是GlobalVariable的情况
                if (auto *TempGV = dyn_cast<GlobalVariable>(Addr)) {
                    GV = TempGV;
                }
                // ConstantExpr的情况，处理getelementptr inbounds操作
                else if (auto *CE = dyn_cast<ConstantExpr>(Addr)) {
                    if (CE->getOpcode() == Instruction::GetElementPtr) {
                        // 检查第一个操作数是否是GlobalVariable
                        if (auto *TempGV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
                            GV = TempGV;
                        }
                    }
                }
                
                if (GV && GV->hasInternalLinkage()) {
                    // Skip global static vars
                    if (GV->getName().find(F.getName()) == std::string::npos) continue;
                    // "func_name.var_name" -> "var_name"
                    std::string prefix = F.getName().str() + ".";
                    size_t prefixLen = prefix.length();
                    varName = GV->getName().substr(prefixLen);

                    SmallVector<DIGlobalVariableExpression *, 1> GVs;
                    GV->getDebugInfo(GVs);
                    if (GVs.empty()) continue;
                    DIGlobalVariableExpression *GVE = GVs[0];
                    if (!GVE) continue;
                    DIGlobalVariable *GVDebug = GVE->getVariable();
                    if (!GVDebug) continue;
                    typeName = parseDITypeName(GVDebug->getType());

                    DL = I.getDebugLoc();
                    isStatic = true;
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
                        
                        // 处理结构体声明的局部变量 (通过dbg.declare)
                        std::string structName = "";
                        DICompositeType *structType = nullptr;
                        
                        // 检查是否是typedef定义的结构体
                        if (auto *derivedType = dyn_cast<DIDerivedType>(DV->getType())) {
                            // 如果是typedef类型，获取其名称作为结构体名称
                            if (derivedType->getTag() == dwarf::DW_TAG_typedef) {
                                structName = derivedType->getName().str();
                                // 获取typedef的基类型
                                if (auto *baseType = derivedType->getBaseType()) {
                                    if (auto *baseComposite = dyn_cast<DICompositeType>(baseType)) {
                                        if (baseComposite->getTag() == dwarf::DW_TAG_structure_type) {
                                            structType = baseComposite;
                                        } else if (auto *baseDerived = dyn_cast<DIDerivedType>(baseType)) {
                                            // 处理嵌套的typedef情况
                                            if (auto *nestedComposite = dyn_cast<DICompositeType>(baseDerived->getBaseType())) {
                                                if (nestedComposite->getTag() == dwarf::DW_TAG_structure_type) {
                                                    structType = nestedComposite;
                                                }
                                            }
                                        }
                                    }
                                }
                            } else if (auto *baseType = dyn_cast<DICompositeType>(derivedType->getBaseType())) {
                                // 其他派生类型，直接获取基类型
                                if (baseType->getTag() == dwarf::DW_TAG_structure_type) {
                                    structName = baseType->getName().str();
                                    structType = baseType;
                                }
                            }
                        } else if (auto *compositeType = dyn_cast<DICompositeType>(DV->getType())) {
                            // 直接获取结构体类型
                            if (compositeType->getTag() == dwarf::DW_TAG_structure_type) {
                                structName = compositeType->getName().str();
                                structType = compositeType;
                            }
                        }
                        if (structName.empty()) {
                            if (auto *AI = dyn_cast<AllocaInst>(DDI->getAddress())) {
                                // 从AllocaInst获取分配的类型
                                if (auto *ST = dyn_cast<StructType>(AI->getAllocatedType()))
                                    structName = ST->getStructName().str();
                            }
                        }
                        if (structType) dumpStructDecl(structType, structName);
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
        // Skip vars not declared by user
        if(name.find('.') != string::npos) continue;

        nlohmann::json globalVar;
        globalVar["name"] = name;
        globalVar["isStatic"] = GV.hasInternalLinkage();

        SmallVector<DIGlobalVariableExpression *, 1> GVs;
        GV.getDebugInfo(GVs);
        if (GVs.empty()) continue;
        DIGlobalVariableExpression *GVE = GVs[0];
        if (!GVE) continue;
        DIGlobalVariable *GVDebug = GVE->getVariable();
        if (!GVDebug) continue;
        unsigned lineNo = GVDebug->getLine();
        DIFile *file = GVDebug->getFile();
        globalVar["line"] = lineNo;
        globalVar["file"] = file->getFilename().str();
        auto typeName = parseDITypeName(GVDebug->getType());

        // 处理结构体声明的全局变量
        if (auto *compositeType = dyn_cast<DICompositeType>(GVDebug->getType())) {
            if (compositeType->getTag() == dwarf::DW_TAG_structure_type) {
                typeName = compositeType->getName().str();
                // 如果typeName为空，尝试从global initializer获取信息
                if (typeName.empty()) {
                    if (auto *ST = dyn_cast<StructType>(GV.getValueType())) {
                        typeName = ST->getStructName().str();
                    }
                }
                dumpStructDecl(compositeType, typeName);
            }
            // DICompositeType还有一种情况：数组变量
        }

        globalVar["type"] = typeName;

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

void PrintConfig::dumpStructDecl(DICompositeType* structType, std::string structName) {
    nlohmann::json structDecl;
    unsigned i = 0;
    for (auto *element : structType->getElements()) {
        if (auto *memberType = dyn_cast<DIDerivedType>(element)) {
            if (memberType->getTag() != dwarf::DW_TAG_member) continue;
            nlohmann::json memberInfo;
            memberInfo["member_index"] = i++;
            memberInfo["type"] = parseDITypeName(memberType->getBaseType());
            structDecl.push_back(memberInfo);
        }
    }
    // 若structName有"struct."前缀，则将其去掉
    if (structName.find("struct.") != std::string::npos)
        structName = structName.substr(7);
    config["structs"][structName] = structDecl;
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
