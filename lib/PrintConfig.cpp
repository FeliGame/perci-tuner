// @Author: Guocheng - NUDT
// Usage: Search declared variables' type.
// --mxp: if true, only search __autofp vars or vars in #pragma region.
// --exclude-file=[.c]: these files won't be scanned
// --exclude-func=[func]: these functions won't be scanned
// Output: config.json

#include "PrintConfig.h"
#include "utils/GlobMatch.hpp"
#include "utils/ParseTypeName.hpp"
#include "utils/ParseMetaDataNode.hpp"

#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/DebugInfo.h>
#if LLVM_VERSION_MAJOR >= 19
#include <llvm/IR/DebugProgramInstruction.h>
#endif
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<bool> EnableMXP("mxp", cl::value_desc("enable pragma & autofp"), cl::desc("true or false"), cl::init(false));
cl::opt<string> ExcludeFiles("exclude-file", cl::value_desc("files skipped to scan"), cl::desc("comma separated src files"), cl::init(""));
cl::opt<string> ExcludeFuncs("exclude-func", cl::value_desc("functions skipped to scan"), cl::desc("comma separated function names"), cl::init(""));
cl::opt<string> IncludeFiles("include-file", cl::value_desc("files included to scan"), cl::desc("comma separated src files"), cl::init(""));
cl::opt<string> IncludeFuncs("include-func", cl::value_desc("functions included to scan"), cl::desc("comma separated function names"), cl::init(""));


// Find Local vars and local static vars
void PrintConfig::findLocalVariables(Function &F) {
    json::Array function;
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
            bool isStatic = false, isParam = false, declFound = false;

            auto dumpLocal = [&]() -> void {
                if (declFound || typeName == "" ||
                    typeName.find("integer") != std::string::npos ||
                    typeName.find("size_t") != std::string::npos ||
                    typeName.find("__intptr_t") != std::string::npos)
                    return;

                if (DL) {  // clang读取dbg.declare调用；flang-new不支持
                    lineNo = DL.getLine();
                    colNo = DL.getCol();
                    auto *localScope = dyn_cast<llvm::DILocalScope>(DL.getScope());
                    fileName = localScope->getNonLexicalBlockFileScope()->getFile()->getFilename().str();
                }
                json::Object localVar;
                localVar["name"] = json::Value(varName);
                localVar["type"] = json::Value(typeName);
                localVar["line"] = json::Value(static_cast<int64_t>(lineNo));
                localVar["column"] = json::Value(static_cast<int64_t>(colNo));
                localVar["file"] = json::Value(fileName);
                localVar["isStatic"] = json::Value(isStatic);
                localVar["isParam"] = json::Value(isParam);
                // Handle __autofp
                if (autoFPRange.count(varName)) {
                    json::Array rangeArr;
                    for (auto &s : autoFPRange[varName])
                        rangeArr.push_back(json::Value(s));
                    localVar["range"] = std::move(rangeArr);
                    searchableVars.insert(varName);
                }
            
                function.push_back(json::Value(std::move(localVar))); 
                declFound = true; // To avoid repeat dump
            };

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
#if LLVM_VERSION_MAJOR >= 19
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
            for (auto &DR : I.getDbgRecordRange()) {
                if (auto *DDI = dyn_cast<DbgVariableRecord>(&DR)) {
                    if (!DDI->isDbgDeclare()) continue;
                    if (auto *DV = DDI->getVariable()) {
#else
            else if (auto *CI = dyn_cast<CallInst>(&I)) {
                if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                    if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
#endif
                        varName = DV->getName();
                        if (varName == "this") continue;
                        typeName = parseDITypeName(DV->getType());
                        if (DV->isParameter() && typeName.find('*') != std::string::npos) continue;  // 跳过函数指针形参
                        declFound = false;  // LLVM22: Struct type alias will be hooked by multi #declare
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
                                // 获取typedef的基类型
                                if (auto *baseType = derivedType->getBaseType()) {
                                    if (auto *baseComposite = dyn_cast<DICompositeType>(baseType)) {
                                        if (baseComposite->getTag() == dwarf::DW_TAG_structure_type) {
                                            structType = baseComposite;
                                            structName = baseComposite->getName().str();
                                            if (structName.empty())
                                                structName = derivedType->getName().str();
                                        }
                                    }
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
                        dumpLocal();
                    }
                }
#if LLVM_VERSION_MAJOR >= 19
                // TODO: LLVM 19+: autofp intrinsics not implemented
#else
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
                                        auto *localObj = localVar.getAsObject();
                                        auto *nameVal = localObj ? localObj->get("name") : nullptr;
                                        if (localObj && nameVal && nameVal->getAsString() == precisionRange[0]) {
                                            json::Array rangeArr;
                                            for (size_t i = 1; i < precisionRange.size(); ++i) {
                                                rangeArr.push_back(json::Value(precisionRange[i]));
                                            }
                                            (*localObj)["range"] = std::move(rangeArr);
                                            break;
                                        }
                                    }
                                } else {
                                    // 保存精度范围到临时map中
                                    SmallVector<std::string> rangeStrs;
                                    for (size_t i = 1; i < precisionRange.size(); ++i)
                                        rangeStrs.push_back(precisionRange[i].str());
                                    autoFPRange[precisionRange[0].str()] = rangeStrs;
                                }
                            }
                        }
                    } else if (name == "llvm.precision.error") {
                        auto *errorMD = getMetadataNode(CI, 0);
                        auto errors = extractStringsFromMD(errorMD);
                        if (auto *configObj = config.getAsObject())
                            (*configObj)["result_error"] = json::Value(errors[1].str());  // errors[0] is var_name
                    }
                }
#endif
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

            dumpLocal();
        }
    }

    json::Array newFunction;
    for (auto &localVar : function) {
        auto *localObj = localVar.getAsObject();
        std::string varName = "";
        if (localObj) {
            if (auto *nameVal = localObj->get("name")) {
                if (auto nameStr = nameVal->getAsString())
                    varName = nameStr->str();
            }
        }
        // If search range not specified, use default range
        if (localObj) {
            auto rangeIt = localObj->find("range");
            if (rangeIt == localObj->end() || !rangeIt->second.getAsArray() || rangeIt->second.getAsArray()->empty()) {
                json::Array rangeArr;
                for (auto &s : defaultPrecisionRange)
                    rangeArr.push_back(json::Value(s));
                (*localObj)["range"] = std::move(rangeArr);
            }
        }
        // Only save referenced vars into config
        if (searchableVars.count(varName) > 0) {
            newFunction.push_back(std::move(localVar));
        }
    }

    if (auto *configObj = config.getAsObject()) {
        json::Value &localVal = (*configObj)["local"];
        if (localVal.getAsObject()) {
            (*localVal.getAsObject())[funcName] = json::Value(std::move(EnableMXP ? newFunction : function));
        } else {
            json::Object newLocal;
            newLocal[funcName] = json::Value(std::move(EnableMXP ? newFunction : function));
            localVal = json::Value(std::move(newLocal));
        }
    }
    autoFPRange.clear();
}

void PrintConfig::findGlobalVariables(Module &M) { 
    json::Array globalVars;

    for (GlobalVariable &GV : M.globals()) {
        if (GV.isDeclaration()) continue;

        auto name = demangle(GV.getName());
        // Skip vars not declared by user
        if(name.find('.') != string::npos) continue;

        json::Object globalVar;
        globalVar["name"] = json::Value(name);
        globalVar["isStatic"] = json::Value(GV.hasInternalLinkage());

        SmallVector<DIGlobalVariableExpression *, 1> GVs;
        GV.getDebugInfo(GVs);
        if (GVs.empty()) continue;
        DIGlobalVariableExpression *GVE = GVs[0];
        if (!GVE) continue;
        DIGlobalVariable *GVDebug = GVE->getVariable();
        if (!GVDebug) continue;
        unsigned lineNo = GVDebug->getLine();
        DIFile *file = GVDebug->getFile();
        string fileName = file->getFilename().str();
        if (!needScanFile(file)) continue;
        globalVar["line"] = json::Value(static_cast<int64_t>(lineNo));
        globalVar["file"] = json::Value(fileName);
        auto typeName = parseDITypeName(GVDebug->getType());
        DICompositeType *structType = nullptr;
        std::string structName = "";

        // 处理结构体声明的全局变量
        if (auto *derivedType = dyn_cast<DIDerivedType>(GVDebug->getType())) {
            if (derivedType->getTag() == dwarf::DW_TAG_typedef) {
                // 获取typedef的基类型
                if (auto *baseType = derivedType->getBaseType()) {
                    if (auto *baseComposite = dyn_cast<DICompositeType>(baseType)) {
                        if (baseComposite->getTag() == dwarf::DW_TAG_structure_type) {
                            structType = baseComposite;
                            structName = baseComposite->getName().str();
                            if (structName.empty())
                                structName = derivedType->getName().str();
                        }
                    }
                }
            }
        } else if (auto *compositeType = dyn_cast<DICompositeType>(GVDebug->getType())) {
            if (compositeType->getTag() == dwarf::DW_TAG_structure_type) {
                typeName = compositeType->getName().str();
                // 如果typeName为空，尝试从global initializer获取信息
                if (typeName.empty()) {
                    if (auto *ST = dyn_cast<StructType>(GV.getValueType())) {
                        typeName = ST->getStructName().str();
                    }
                }
            }
        }
        if (structType) dumpStructDecl(structType, structName);

        globalVar["type"] = json::Value(typeName);

        // handle __autofp
        if (MDNode *MD = GV.getMetadata("precision_range")) {
            SmallVector<string> precisionRange;
            for (const MDOperand &MDOp : drop_begin(MD->operands(), 1)) {
                if (auto *MDStr = dyn_cast<MDString>(MDOp.get())) {
                    precisionRange.push_back(MDStr->getString().str());
                }
            }
            json::Array rangeArr;
            for (auto &s : precisionRange)
                rangeArr.push_back(json::Value(s));
            globalVar["range"] = std::move(rangeArr);
            if (EnableMXP)
                globalVars.push_back(json::Value(std::move(globalVar)));  // only __autofp globalVar available
        }
        if (!EnableMXP)
            globalVars.push_back(json::Value(std::move(globalVar)));
    }
    if (auto *configObj = config.getAsObject())
        (*configObj)["global"] = json::Value(std::move(globalVars));
}

void PrintConfig::dumpStructDecl(DICompositeType* structType, std::string structName) {
    if (structName.empty()) return;
    json::Array structDecl;
    unsigned i = 0;
    for (auto *element : structType->getElements()) {
        if (auto *memberType = dyn_cast<DIDerivedType>(element)) {
            if (memberType->getTag() != dwarf::DW_TAG_member) continue;
            json::Object memberInfo;
            memberInfo["member_index"] = json::Value(static_cast<int64_t>(i++));
            memberInfo["type"] = json::Value(parseDITypeName(memberType->getBaseType()));
            structDecl.push_back(json::Value(std::move(memberInfo)));
        }
    }
    // 若structName有"struct."前缀，则将其去掉
    if (structName.find("struct.") != std::string::npos)
        structName = structName.substr(7);
    if (auto *configObj = config.getAsObject()) {
        json::Value &structsVal = (*configObj)["structs"];
        if (auto *structsObj = structsVal.getAsObject())
            (*structsObj)[structName] = json::Value(std::move(structDecl));
        else {
            json::Object newStructs;
            newStructs[structName] = json::Value(std::move(structDecl));
            structsVal = json::Value(std::move(newStructs));
        }
    }
}

void PrintConfig::findFunctionCalls(Function &F) {
    json::Array funcInfo;
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
                        json::Object callInfo;
                        callInfo["type"] = json::Value(callee->getName().str());
                        if (loc) {
                            callInfo["file"] = json::Value(loc->getFilename().str());
                            callInfo["line"] = json::Value(static_cast<int64_t>(loc.getLine()));
                            callInfo["column"] = json::Value(static_cast<int64_t>(loc.getCol()));
                        }
                        funcInfo.push_back(json::Value(std::move(callInfo)));
                    }
                }
            }
        }
    }
    if (auto *configObj = config.getAsObject()) {
        json::Value &callsVal = (*configObj)["calls"];
        if (auto *callsObj = callsVal.getAsObject())
            (*callsObj)[demangle(F.getName())] = json::Value(std::move(funcInfo));
        else {
            json::Object newCalls;
            newCalls[demangle(F.getName())] = json::Value(std::move(funcInfo));
            callsVal = json::Value(std::move(newCalls));
        }
    }
}

bool PrintConfig::runOnFunction(Function &F) {
    if (!needScanFunction(F)) {
        autoFPRange.clear();
        return false;
    }

    findLocalVariables(F);
    findFunctionCalls(F);
    return false;
}

void PrintConfig::parseIncludeAndExcludeLists() {
    auto parseCommaList = [](std::string raw) {
        vector<string> items;
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t comma = raw.find(',', pos);
            std::string token;
            if (comma == std::string::npos) {
                token = raw.substr(pos);
                pos = raw.size();
            } else {
                token = raw.substr(pos, comma - pos);
                pos = comma + 1;
            }
            size_t begin = token.find_first_not_of(" \t\n\r");
            size_t end = token.find_last_not_of(" \t\n\r");
            if (begin != std::string::npos)
                items.push_back(token.substr(begin, end - begin + 1));
        }
        return items;
    };

    excludeFiles.clear();
    excludeFuncs.clear();
    includeFiles.clear();
    includeFuncs.clear();

    excludeFiles = parseCommaList(ExcludeFiles);
    excludeFuncs = parseCommaList(ExcludeFuncs);
    includeFiles = parseCommaList(IncludeFiles);
    includeFuncs = parseCommaList(IncludeFuncs);
}

bool PrintConfig::needScanFile(DIFile *file) {
    if (!file) return true;
    std::string fileName = file->getFilename().str();
    std::string dir = file->getDirectory().str();
    std::string path = dir.empty() ? fileName : dir + "/" + fileName;
    return (includeFiles.size() < 1 || matchAnyPattern(includeFiles, {fileName, path})) && 
           !matchAnyPattern(excludeFiles, {fileName, path});
}

bool PrintConfig::needScanFunction(Function &F) {
    auto funcName = demangle(F.getName());
    return (includeFuncs.size() < 1 || matchAnyPattern(includeFuncs, {funcName, F.getName().str()})) && 
           !matchAnyPattern(excludeFuncs, {funcName, F.getName().str()});
}

bool PrintConfig::runOnModule(Module &module) {
    parseIncludeAndExcludeLists();
    
    // Initialize config as an Object
    config = json::Value(json::Object());

    findGlobalVariables(module);

    for(auto &F : module) {
        string name = F.getName().str();
        if (F.isDeclaration()) continue;

        if (auto *SP = F.getSubprogram())
            if (!needScanFile(SP->getFile())) continue;

        if(name == "main" || !F.use_empty())
            runOnFunction(F);
    }

    std::error_code errorInfo; 
    raw_fd_ostream outfile("config.json", errorInfo);
    outfile << formatv("{0:4}", json::Value(std::move(config)));

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