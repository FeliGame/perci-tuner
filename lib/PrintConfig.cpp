// @Author: Guocheng - NUDT
// Usage: Search declared variables' type.
// --mxp: if true, only search __autofp vars or vars in #pragma region.
// --exclude-file=[.c]: these files won't be scanned
// --exclude-func=[func]: these functions won't be scanned
// --exclude-variable=[global::var,func::var]: scoped variables won't be emitted
// --exclude-type=[type]: variables and struct declarations of these base types won't be emitted
// Output: config.json

#include "PrintConfig.h"
#include "utils/ParseTypeName.hpp"
#include "utils/ParseMetaDataNode.hpp"
#include "utils/GlobMatch.hpp"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

#include <algorithm>
#include <cctype>
#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<bool> EnableMXP("mxp", cl::value_desc("enable pragma & autofp"), cl::desc("true or false"), cl::init(false));
cl::opt<string> ExcludeFiles("exclude-file", cl::value_desc("files skipped to scan"), cl::desc("comma separated src files"), cl::init(""));
cl::opt<string> ExcludeFuncs("exclude-func", cl::value_desc("functions skipped to scan"), cl::desc("comma separated function names"), cl::init(""));
cl::opt<string> ExcludeVariables("exclude-variable", cl::value_desc("variables skipped in config"), cl::desc("comma separated names: global::var or function::var"), cl::init(""));
cl::opt<string> ExcludeTypes("exclude-type", cl::value_desc("types skipped in config"), cl::desc("comma separated base type names"), cl::init(""));
cl::opt<string> IncludeFiles("include-file", cl::value_desc("files included to scan"), cl::desc("comma separated src files"), cl::init(""));
cl::opt<string> IncludeFuncs("include-func", cl::value_desc("functions included to scan"), cl::desc("comma separated function names"), cl::init(""));

namespace {

struct DiscoveredStruct {
    DICompositeType *type = nullptr;
    std::string name;
};

// Debug types are commonly wrapped as
// pointer -> const/typedef -> array -> struct.  Structure discovery must not
// depend on the variable being declared directly as the typedef itself.
DiscoveredStruct discoverStructType(DIType *type) {
    std::string typedefName;

    // A short depth limit protects against malformed recursive metadata while
    // still allowing all ordinary qualifier/pointer/array nesting.
    for (unsigned depth = 0; type && depth < 32; ++depth) {
        if (auto *composite = dyn_cast<DICompositeType>(type)) {
            if (composite->getTag() == dwarf::DW_TAG_structure_type ||
                composite->getTag() == dwarf::DW_TAG_class_type ||
                composite->getTag() == dwarf::DW_TAG_union_type) {
                std::string name = composite->getName().str();
                if (name.empty())
                    name = typedefName;
                return {composite, name};
            }

            // Arrays are DICompositeType nodes whose base type may ultimately
            // be a structure.
            if (composite->getTag() == dwarf::DW_TAG_array_type) {
                type = composite->getBaseType();
                continue;
            }
            break;
        }

        auto *derived = dyn_cast<DIDerivedType>(type);
        if (!derived)
            break;
        if (derived->getTag() == dwarf::DW_TAG_typedef &&
            typedefName.empty() && !derived->getName().empty()) {
            typedefName = derived->getName().str();
        }
        type = derived->getBaseType();
    }
    return {};
}

bool isFloatingSearchType(const std::string &typeName) {
    return typeName.find("float") != std::string::npos ||
           typeName.find("double") != std::string::npos ||
           typeName.find("__fp16") != std::string::npos ||
           typeName.find("_Float16") != std::string::npos ||
           typeName.find("__half") != std::string::npos;
}

} // namespace


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
                    auto discovered = discoverStructType(GVDebug->getType());
                    if (discovered.type)
                        dumpStructDecl(discovered.type, discovered.name);

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
                        auto discovered = discoverStructType(DV->getType());
                        if (discovered.type)
                            dumpStructDecl(discovered.type, discovered.name);

                        // Pointer parameters are not independent storage
                        // candidates, but their pointee record definitions
                        // above still contribute searchable fields.
                        if (DV->isParameter() &&
                            typeName.find('*') != std::string::npos)
                            continue;
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
                // 局部变量/参数使用 function::name 精确限定作用域；
                // 类型规则也在写入 config 前统一应用。
                if (isExcludedVariable(funcName, varName) || isExcludedType(typeName))
                    continue;
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
        string fileName = file->getFilename().str();
        if (!needScanFile(file)) continue;
        globalVar["line"] = lineNo;
        globalVar["file"] = fileName;
        auto typeName = parseDITypeName(GVDebug->getType());
        auto discovered = discoverStructType(GVDebug->getType());
        if (discovered.type)
            dumpStructDecl(discovered.type, discovered.name);

        globalVar["type"] = typeName;

        // 全局变量使用 variable:global::name；变量和类型规则都在生成
        // 搜索空间时直接过滤，后续搜索算法不会再看到这些对象。
        if (isExcludedVariable("", name) || isExcludedType(typeName))
            continue;

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
    if (structName.empty()) return;
    // type:Foo 不仅排除 Foo 类型的变量，也排除 Foo 的字段搜索空间。
    if (isExcludedType(structName)) return;
    nlohmann::json structDecl;
    unsigned i = 0;
    for (auto *element : structType->getElements()) {
        if (auto *memberType = dyn_cast<DIDerivedType>(element)) {
            if (memberType->getTag() != dwarf::DW_TAG_member) continue;
            unsigned memberIndex = i++;
            std::string memberTypeName =
                parseDITypeName(memberType->getBaseType());
            // Integer/control members are record layout metadata, not
            // mixed-precision candidates. Keep the original member index so
            // a later floating member still maps to the correct AST field.
            if (!isFloatingSearchType(memberTypeName))
                continue;
            nlohmann::json memberInfo;
            memberInfo["member_index"] = memberIndex;
            memberInfo["type"] = memberTypeName;
            structDecl.push_back(memberInfo);
        }
    }
    // 若structName有"struct."前缀，则将其去掉
    if (structName.find("struct.") != std::string::npos)
        structName = structName.substr(7);
    if (!structDecl.empty())
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
    if (!needScanFunction(F)) {
        autoFPRange.clear();
        return false;
    }

    findLocalVariables(F);
    findFunctionCalls(F);
    return false;
}

void PrintConfig::findExcludedFunctionInterfaces(Module &M) {
    nlohmann::json boundaries = nlohmann::json::object();

    for (auto &F : M) {
        if (F.isDeclaration() || needScanFunction(F))
            continue;

        auto *SP = F.getSubprogram();
        if (SP && !needScanFile(SP->getFile()))
            continue;

        std::string functionName = demangle(F.getName());
        nlohmann::json boundary;
        boundary["name"] = functionName;
        boundary["symbol"] = F.getName().str();
        if (SP) {
            boundary["file"] = SP->getFile()->getFilename().str();
            boundary["line"] = SP->getLine();
        }

        nlohmann::json parameters = nlohmann::json::array();
        unsigned parameterIndex = 0;
        for (auto &Arg : F.args()) {
            nlohmann::json parameter;
            parameter["index"] = parameterIndex++;
            parameter["name"] = Arg.hasName() ? Arg.getName().str() : "";
            parameter["type"] = parseTypeName(Arg.getType());
            // This metadata describes a derived interface only. It is never
            // copied into config["local"] and therefore is not searchable.
            parameter["searchable"] = false;
            parameters.push_back(parameter);
        }
        boundary["parameters"] = parameters;

        nlohmann::json calls = nlohmann::json::array();
        for (auto *User : F.users()) {
            auto *Call = dyn_cast<CallBase>(User);
            if (!Call || Call->getCalledFunction() != &F)
                continue;

            nlohmann::json call;
            if (auto *Caller = Call->getFunction())
                call["caller"] = demangle(Caller->getName());
            if (auto Loc = Call->getDebugLoc()) {
                call["file"] = Loc->getFilename().str();
                call["line"] = Loc.getLine();
                call["column"] = Loc.getCol();
            }
            nlohmann::json arguments = nlohmann::json::array();
            nlohmann::json bindings = nlohmann::json::array();
            for (unsigned I = 0; I < Call->arg_size(); ++I) {
                Value *Actual = Call->getArgOperand(I);
                arguments.push_back(parseTypeName(Actual->getType()));

                nlohmann::json binding;
                binding["parameter_index"] = I;
                if (I < F.arg_size()) {
                    auto Parameter = F.arg_begin();
                    std::advance(Parameter, I);
                    binding["parameter_name"] =
                        Parameter->hasName() ? Parameter->getName().str() : "";
                }
                binding["argument_type"] = parseTypeName(Actual->getType());
                binding["pointer_interface"] = Actual->getType()->isPointerTy();

                // With LLVM opaque pointers the pointee element type is no
                // longer present in IR. Preserve the positional binding and,
                // where possible, recover the caller-side storage object
                // loaded from an alloca. TransformType resolves the concrete
                // element type later from the source AST.
                Value *Storage = Actual->stripPointerCasts();
                if (auto *Load = dyn_cast<LoadInst>(Storage))
                    Storage = Load->getPointerOperand()->stripPointerCasts();
                binding["argument_storage"] =
                    Storage->hasName() ? Storage->getName().str() : "";
                bindings.push_back(binding);
            }
            call["argument_types"] = arguments;
            call["bindings"] = bindings;
            calls.push_back(call);
        }
        boundary["calls"] = calls;
        boundaries[functionName] = boundary;
    }

    config["boundary_interfaces"] = boundaries;
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
    excludeVariables.clear();
    excludeTypes.clear();
    includeFiles.clear();
    includeFuncs.clear();

    excludeFiles = parseCommaList(ExcludeFiles);
    excludeFuncs = parseCommaList(ExcludeFuncs);
    excludeVariables = parseCommaList(ExcludeVariables);
    excludeTypes = parseCommaList(ExcludeTypes);
    includeFiles = parseCommaList(IncludeFiles);
    includeFuncs = parseCommaList(IncludeFuncs);

    // 兼容旧命令行：历史上的无作用域变量规则只按全局变量解释。
    // 新 excluded.txt 必须显式写 global::name 或 function::name。
    for (auto &variable : excludeVariables) {
        if (variable.find("::") == std::string::npos) {
            errs() << ">>> Perci-Tuner: warning: unscoped exclude-variable '"
                   << variable << "' is deprecated; treating it as 'global::"
                   << variable << "'.\n";
            variable = "global::" + variable;
        }
    }
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
    auto matchesFunctionRule = [](const vector<string> &patterns,
                                  const vector<string> &candidates) {
        for (const auto &pattern : patterns) {
            for (const auto &candidate : candidates) {
                bool hasGlob = pattern.find('*') != std::string::npos ||
                               pattern.find('?') != std::string::npos;
                if ((hasGlob && matchGlobPattern(pattern, candidate)) ||
                    (!hasGlob && pattern == candidate))
                    return true;
            }
        }
        return false;
    };
    vector<string> candidates = {funcName, F.getName().str()};
    return (includeFuncs.empty() || matchesFunctionRule(includeFuncs, candidates)) &&
           !matchesFunctionRule(excludeFuncs, candidates);
}

bool PrintConfig::isExcludedVariable(const std::string &functionName,
                                     const std::string &variableName) {
    // 每个变量只生成一个带作用域候选，确保同名全局、局部和参数互不误伤。
    // 全局写 global::name，局部/参数写 function::name。
    vector<string> candidates = {
        (functionName.empty() ? "global" : functionName) + "::" + variableName
    };

    // 变量名必须精确匹配；只有规则显式包含 * 或 ? 时才按 glob 匹配。
    // 作用域前缀进一步保证 global::data 不会排除 foo::data。
    for (const auto &pattern : excludeVariables) {
        for (const auto &candidate : candidates) {
            bool hasGlob = pattern.find('*') != std::string::npos ||
                           pattern.find('?') != std::string::npos;
            if ((hasGlob && matchGlobPattern(pattern, candidate)) ||
                (!hasGlob && pattern == candidate))
                return true;
        }
    }
    return false;
}

bool PrintConfig::isExcludedType(const std::string &typeName) {
    std::string baseType = typeName;

    // ParseTypeName may produce forms such as "const OptionData_*" or
    // "timespec[2]". A type rule names the underlying user-visible type,
    // so remove qualifiers, pointer/array decorations and LLVM's trailing
    // underscore used for some typedef-struct names before matching.
    const vector<string> qualifiers = {"const", "volatile", "restrict", "struct", "class"};
    for (const auto &qualifier : qualifiers) {
        size_t pos = 0;
        while ((pos = baseType.find(qualifier, pos)) != std::string::npos) {
            bool leftBoundary = pos == 0 ||
                !isalnum(static_cast<unsigned char>(baseType[pos - 1]));
            size_t wordEnd = pos + qualifier.size();
            bool rightBoundary = wordEnd == baseType.size() ||
                !isalnum(static_cast<unsigned char>(baseType[wordEnd]));
            if (leftBoundary && rightBoundary)
                baseType.replace(pos, qualifier.size(), " ");
            else
                pos = wordEnd;
        }
    }
    size_t arrayPos = baseType.find('[');
    if (arrayPos != std::string::npos)
        baseType.erase(arrayPos);
    baseType.erase(remove(baseType.begin(), baseType.end(), '*'), baseType.end());
    baseType.erase(remove(baseType.begin(), baseType.end(), '&'), baseType.end());
    size_t begin = baseType.find_first_not_of(" \t\n\r");
    size_t end = baseType.find_last_not_of(" \t\n\r");
    baseType = begin == std::string::npos ? "" : baseType.substr(begin, end - begin + 1);
    while (!baseType.empty() && baseType.back() == '_')
        baseType.pop_back();

    // 类型与变量一样采用精确/glob 匹配，避免短类型名误伤其他类型。
    for (const auto &pattern : excludeTypes) {
        for (const auto &candidate : {typeName, baseType}) {
            bool hasGlob = pattern.find('*') != std::string::npos ||
                           pattern.find('?') != std::string::npos;
            if ((hasGlob && matchGlobPattern(pattern, candidate)) ||
                (!hasGlob && pattern == candidate))
                return true;
        }
    }
    return false;
}

bool PrintConfig::runOnModule(Module &module) {
    parseIncludeAndExcludeLists();
    
    findGlobalVariables(module);
    findExcludedFunctionInterfaces(module);

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
