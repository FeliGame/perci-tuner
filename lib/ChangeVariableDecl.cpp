// @Author: Guocheng & Guosiyuan NUDT
// Usage: Change variables' precision according to json.
// Input: Variable's type config
// Output: Precision-Changed IR

#include "ChangeVariableDecl.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/IRBuilder.h>
#include <functional>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APSInt.h>
#include <llvm/ADT/Twine.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Operator.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Config/llvm-config.h>

#include <fstream>
#include <sstream>

using namespace std;
using namespace llvm;


cl::opt<string> VariableConfigName("variable-config", cl::value_desc("filename"), cl::desc("Variable's Type Config"), cl::init("config.json"));

void ChangeVariableDecl::parseConfig() {
    std::ifstream configFile(VariableConfigName.c_str());
    if (!configFile.is_open()) {
        // errs() << "Error: Failed to open " << VariableConfigName << "\n";
        exit(1);
    }

    configContents.assign((std::istreambuf_iterator<char>(configFile)),
                          std::istreambuf_iterator<char>());
    auto parsed = json::parse(StringRef(configContents));
    if (!parsed) {
        exit(1);
    }
    config = std::move(*parsed);

    json::Value* localConfig = config.getAsObject()->get("local");
    if (!localConfig || localConfig->kind() == json::Value::Null) return;

    auto* localObj = localConfig->getAsObject();
    if (!localObj) return;

    for (auto &funcEntry : *localObj)
        includedFunctions.insert(funcEntry.first.str());

    for (auto &funcEntry : *localObj) {
        if (auto* funcArr = funcEntry.second.getAsArray()) {
            for (auto &varEntry : *funcArr) {
                auto* varObj = varEntry.getAsObject();
                if (!varObj) continue;

                auto fileOpt = varObj->getString("file");
                std::string file = fileOpt ? fileOpt->str() : "";
                auto lineOpt = varObj->getNumber("line");
                int line = lineOpt ? (int)*lineOpt : 0;
                auto columnOpt = varObj->getNumber("column");
                int column = columnOpt ? (int)*columnOpt : 0;

                std::string key = file + ":" + std::to_string(line) + ":" + std::to_string(column);
                configMap.insert_or_assign(key, varEntry);
            }
        }
    }
}

// Initialize base-type maps for globals and locals before DIBuilder exists.
void ChangeVariableDecl::initBaseTypeMaps(Module &module) {
    auto &context = module.getContext();

    // 为每个alloca和GV都赋予基类型，防止map空调用
    for (auto &F : module) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                if (auto *alloca = dyn_cast<AllocaInst>(&I)) {
                    if (!localBaseTypeMap.count(alloca)) {
                        Type *baseType = alloca->getAllocatedType();
                        while (baseType->isArrayTy())
                            baseType = baseType->getArrayElementType();
                        localBaseTypeMap[alloca] = baseType;
                    }
                }
            }
        }
    }

    for (auto &GV : module.globals()) {
        if (GV.isDeclaration()) continue;
        if (!globalBaseTypeMap.count(&GV)) {
            Type *baseType = GV.getValueType();
            while (baseType->isArrayTy())
                baseType = baseType->getArrayElementType();
            globalBaseTypeMap[&GV] = baseType;
        }
    }

    // Pre-initialize global base type map from config["global"] (if present)
    json::Value* globalVal = config.getAsObject()->get("global");
    if (globalVal && globalVal->kind() != json::Value::Null) {
        if (auto* globalArr = globalVal->getAsArray()) {
            for (auto &globalEntry : *globalArr) {
                auto* entryObj = globalEntry.getAsObject();
                if (!entryObj) continue;
                auto nameOpt = entryObj->getString("name");
                std::string varName = nameOpt ? nameOpt->str() : "";
                auto typeOpt = entryObj->getString("type");
                std::string typeName = typeOpt ? typeOpt->str() : "";
                auto *targetType = typeNameToBasicType(typeName, context);
                if (!targetType) continue;

                auto *baseType = targetType;
                while (baseType->isArrayTy())
                    baseType = baseType->getArrayElementType();

                for (auto &GV : module.globals()) {
                    if (GV.isDeclaration()) continue;
                    auto name = demangle(GV.getName().str());
                    // Skip compiler-internal or mangled symbols
                    if (name.find('.') != string::npos) continue;
                    if (name == varName) {
                        globalBaseTypeMap[&GV] = baseType;
                        break;
                    }
                }
            }
        }
    }

    // Pre-initialize local base type map from configMap (local entries)
    for (auto &F : module) {
        for (auto &BB : F) {
            for (auto &I : BB) {
                // clang-style debug declare
                if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                    if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                        auto loc = DDI->getDebugLoc();
                        if (!loc) continue;
                        auto fileName = DV->getFile()->getFilename().str();
                        std::string key = fileName + ":" + std::to_string(loc.getLine()) + ":" + std::to_string(loc.getCol());
                        auto it = configMap.find(key);
                        if (it == configMap.end()) continue;
                        auto &configEntry = it->second;
                        auto* entryObj = configEntry.getAsObject();
                        auto typeOpt = entryObj->getString("type");
                        std::string typeName = typeOpt ? typeOpt->str() : "";
                        auto *targetType = typeNameToBasicType(typeName, F.getContext());
                        if (!targetType) continue;

                        Type *baseType = targetType;
                        while (baseType->isArrayTy()) baseType = baseType->getArrayElementType();

                        if (auto *addr = dyn_cast<AllocaInst>(DDI->getAddress())) {
                            localBaseTypeMap[addr] = baseType;
                        }

                        // 如果该配置项标注为参数，则初始化函数声明映射与指针形参基类型映射
                        if (entryObj->find("isParam") != entryObj->end() && entryObj->getBoolean("isParam").value_or(false)) {
                            StringRef varName = DV->getName();
                            unsigned argIndex = 0;
                            for (auto argIt = F.arg_begin(); argIt != F.arg_end(); ++argIt, ++argIndex) {
                                if (argIt->getName() == varName) {
                                    funcDeclTypeMap[&F][argIndex] = targetType;
                                    if (argIt->getType()->isPointerTy()) {
                                        argPtrBaseTypeMap[&*argIt] = baseType;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }

                // flang / LLVM19-style alloca metadata
                if (auto *alloca = dyn_cast<AllocaInst>(&I)) {
                    if (auto *N = alloca->getMetadata("dbg")) {
                        if (auto *Loc = dyn_cast<DILocation>(N)) {
                            unsigned lineNo = Loc->getLine();
                            unsigned colNo = Loc->getColumn();
                            std::string fileName = "";
                            if (auto *Scope = Loc->getScope())
                                fileName = Scope->getFilename().str();
                            std::string key = fileName + ":" + std::to_string(lineNo) + ":" + std::to_string(colNo);
                            auto it = configMap.find(key);
                            if (it == configMap.end()) continue;
                            auto &configEntry = it->second;
                            auto* entryObj = configEntry.getAsObject();
                            auto typeOpt = entryObj->getString("type");
                            std::string typeName = typeOpt ? typeOpt->str() : "";
                            auto *targetType = typeNameToBasicType(typeName, F.getContext());
                            if (!targetType) continue;

                            Type *baseType = targetType;
                            while (baseType->isArrayTy()) baseType = baseType->getArrayElementType();
                            localBaseTypeMap[alloca] = baseType;

                            // 如果该配置项标注为参数，则将其与函数形参关联（若alloca名字对应参数名）
                            if (entryObj->find("isParam") != entryObj->end() && entryObj->getBoolean("isParam").value_or(false)) {
                                std::string varName = "";
                                if (alloca->hasName()) varName = alloca->getName().str();
                                unsigned argIndex = 0;
                                for (auto argIt = F.arg_begin(); argIt != F.arg_end(); ++argIt, ++argIndex) {
                                    if (argIt->getName() == varName) {
                                        funcDeclTypeMap[&F][argIndex] = targetType;
                                        if (argIt->getType()->isPointerTy())
                                            argPtrBaseTypeMap[&*argIt] = baseType;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

Type* ChangeVariableDecl::stripArrayType(Type *type) {
    while (auto *arrayType = dyn_cast<ArrayType>(type))
        type = arrayType->getElementType();
    return type;
}

Type* ChangeVariableDecl::replaceArrayBaseType(Type *type, Type *baseType) {
    if (auto *arrayType = dyn_cast<ArrayType>(type)) {
        auto *newElementType = replaceArrayBaseType(arrayType->getElementType(), baseType);
        return ArrayType::get(newElementType, arrayType->getArrayNumElements());
    }
    return baseType;
}

Align ChangeVariableDecl::getAlign(Type* targetType) {
    if (targetType->isPointerTy()) {
        return DL->getPointerPrefAlignment();
    } else if (auto *arrayType = dyn_cast<ArrayType>(targetType)) {
        return getAlign(arrayType->getElementType());
    } else {
        return DL->getABITypeAlign(targetType);
    }
}

AllocaInst* ChangeVariableDecl::changeAllocaInst(AllocaInst *alloca, Type *newType) {
    auto *oldType = alloca->getAllocatedType();
    if (oldType->isPointerTy()) {
        // 对于指针类型，保存指向类型的基本类型
        pointeeTypeMap[alloca] = newType;
    } else if (oldType->isFloatingPointTy()) {
        // errs() << "[change alloca] " << *alloca << "->";
        // 对于非指针类型，更改AllocaInst的类型
        alloca->setAllocatedType(newType);
        alloca->setAlignment(getAlign(newType));
        // errs() << *alloca << '\n';
    } else {
        return nullptr;
    }
    newAllocas.insert(alloca);

    // 存储局部变量到目标类型的映射
    Type *baseType = newType;
    while (baseType->isArrayTy()) {
        baseType = baseType->getArrayElementType();
    }
    localBaseTypeMap[alloca] = baseType;
    // errs() << "[local base type] " << alloca->getName() << " -> " << *baseType << '\n';
    
    // 变换DI类型
    for (auto &BB : *(alloca->getFunction())) {
        for (auto &I : BB) {
            if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                if (DDI->getAddress() == alloca) {
                    if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                        DIType *diType = DV->getType();
                        DIType *finalDIType = nullptr;
                        std::string typeSuffix;
                        
                        // 如果是指针类型，需要替换DIDerivedType的基类型
                        if (diType && isa<DIDerivedType>(diType)) {
                            auto *derivedType = dyn_cast<DIDerivedType>(diType);
                            
                            // 创建新的DIType作为基类型
                            DIType *newBaseDIType = createDIType(newType, DV->getScope(), DIB);
                            if (newBaseDIType) {
                                // 创建新的DIDerivedType，保留指针类型标签
                                finalDIType = DIB->createPointerType(
                                    newBaseDIType,
                                    derivedType->getSizeInBits()
                                );
                                typeSuffix = "*";
                            }
                        } else {
                            // 对于非指针类型，直接创建DIType
                            finalDIType = createDIType(newType, DV->getScope(), DIB);
                        }
                        
                        if (!finalDIType) continue;
                        // 创建新的DILocalVariable
                        auto *newDV = DIB->createAutoVariable(
                            DV->getScope(),
                            DV->getName(),
                            DV->getFile(),
                            DV->getLine(),
                            finalDIType,
                            DV->isArtificial(),
                            DV->getFlags()
                        );
                        DDI->setVariable(newDV);
                        // errs() << "[update alloca's DItype] " << DV->getName() << " type: " << *newType << typeSuffix << '\n';
                    }
                }
            }
        }
    }

    return alloca;
}

void ChangeVariableDecl::repairAllocaUsers(AllocaInst* alloca) {
    auto newType = alloca->getAllocatedType();
    auto align = getAlign(newType);
    for (auto *user : alloca->users()) {
        if (auto load = dyn_cast<LoadInst>(user)) {
            auto *newLoad = load;
            auto *oldType = load->getType();

            if (oldType->isPointerTy()) {  // keep pointee type
                if (auto* alloca = findOriginalAlloca(load->getPointerOperand()))
                    newType = pointeeTypeMap[alloca];
            } else if (oldType == newType) {
                continue;
            } else {
                newLoad = new LoadInst(newType, load->getPointerOperand(), "", load->getIterator());
                newLoad->setAlignment(align);

                load->replaceAllUsesWith(newLoad);
                instToErase.insert(load);    // erase later to avoid broken iterator
            }
            for (auto *user : newLoad->users())
                repairUserType(newLoad, user, newType);
        }
        else repairUserType(alloca, user, newType);
    }
}

// Insert fpext/fptrunc cast if needed
Value *ChangeVariableDecl::insertCast(Value *val, Type *targetType, Instruction *insertBefore) {
    if (val->getType() == targetType) {
        return val; 
    }
#if LLVM_VERSION_MAJOR < 19
    if (auto *constantVal = dyn_cast<Constant>(val)) {
        if (targetType->isFloatingPointTy()) {
            return ConstantExpr::getCast(CastInst::FPExt, constantVal, targetType);
        }
        return nullptr;
    }
#endif

    IRBuilder<> builder(insertBefore);

    if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy())
        return builder.CreateFPCast(val, targetType, "fp_cast");
    if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy())
        return builder.CreateSIToFP(val, targetType, "int_to_fp");
    if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy())
        return builder.CreateFPToSI(val, targetType, "fp_to_int");
    if (val->getType()->isIntegerTy() && targetType->isIntegerTy())
        return builder.CreateIntCast(val, targetType, /*isSigned=*/true, "int_cast");
    
    // errs() << "[unsupported cast] " << *val->getType() << "->" << *targetType << "\n";
    return nullptr;
}

Type* ChangeVariableDecl::findHighestPrecisionType(BinaryOperator *binOp) {
    Type *highestType = nullptr;
    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
        // 浮点常数不锁定类型
        if (isa<Constant>(operand) && operand->getType()->isFloatingPointTy()) continue;
        auto *operandType = operand->getType();
        // if op is CastInst, use SrcType
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            if (castInst->getSrcTy()->isFloatingPointTy()) {
                operandType = castInst->getSrcTy();
            } else if (castInst->getSrcTy()->isIntegerTy()) {
                // integer -> floating
                unsigned bits = castInst->getSrcTy()->getIntegerBitWidth();
                if (bits <= 32) {
                    operandType = Type::getFloatTy(binOp->getContext());
                } else {
                    operandType = Type::getDoubleTy(binOp->getContext());
                }
            }
        }
        if (!highestType || operandType->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
            highestType = operandType;
        }
    }
    return highestType;
}

// Same as BinOp
Type* ChangeVariableDecl::findHighestPrecisionType(FCmpInst *fcmp) {
    Type *highestType = nullptr;
    for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
        auto *operand = fcmp->getOperand(i);
        // 浮点常数不锁定类型
        if (isa<Constant>(operand) && operand->getType()->isFloatingPointTy()) continue;
        auto *operandType = operand->getType();
        // if op is CastInst, use SrcType
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            if (castInst->getSrcTy()->isFloatingPointTy()) {
                operandType = castInst->getSrcTy();
            } else if (castInst->getSrcTy()->isIntegerTy()) {
                // integer -> floating
                unsigned bits = castInst->getSrcTy()->getIntegerBitWidth();
                if (bits <= 32) {
                    operandType = Type::getFloatTy(fcmp->getContext());
                } else {
                    operandType = Type::getDoubleTy(fcmp->getContext());
                }
            }
        }
        if (!highestType || operandType->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
            highestType = operandType;
        }
    }
    return highestType;
}

Type* ChangeVariableDecl::findHighestPrecisionType(PHINode* phi) {
    Type *highestType = nullptr;
    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        Value* val = phi->getIncomingValue(i);
        if (!highestType || val->getType()->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
            highestType = val->getType();
        }
    }
    return highestType;
}

Instruction* ChangeVariableDecl::repairReturnInst(ReturnInst* ret) {
    // errs() << "[repair return]" << *ret << " -> ";
    Value *retVal = ret->getReturnValue();
    if (!retVal) return nullptr;  // void return
    
    Type *funcReturnType = ret->getFunction()->getReturnType();
    if (retVal->getType() != funcReturnType) {
        if (auto *castValue = insertCast(retVal, funcReturnType, ret)) {
            ret->setOperand(0, castValue);
            return dyn_cast<Instruction>(ret);
        }
    }
    // errs() << *ret << '\n';
    return nullptr;  // nothing changed
}

Instruction* ChangeVariableDecl::repairLoadInst(LoadInst* load, Type* targetType, unsigned align) {
    if (load->getType()->isIntegerTy()) return nullptr; // ignore integer load
    // errs() << "[repair load] " << *load << "->" << *targetType << "\n";
    if (load->getType()->isPointerTy()) return load;
    auto *ptrOperand = load->getPointerOperand();
    Type* loadType = targetType;
    if (auto *allocaInst = findOriginalAlloca(ptrOperand))
        if (!loadType)
            loadType = pointeeTypeMap[allocaInst];
    if (loadType) {
        if (auto *arrayType = dyn_cast<ArrayType>(loadType))
            loadType = arrayType->getElementType();
        if (load->getType()->isPointerTy())
            return load;
        if (loadType != load->getType()) {
            auto *newLoad = new LoadInst(loadType, ptrOperand, load->getName(), load->isVolatile(), load->getIterator());
            newLoad->setAlignment(align ? Align(align) : getAlign(loadType));
            newLoad->setDebugLoc(load->getDebugLoc());
            load->replaceAllUsesWith(newLoad);
            instToErase.insert(load);
            // errs() << "[repair load] " << *load << "->" << *newLoad << '\n';
            return newLoad;
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairStoreInst(StoreInst* store, Type* targetType, unsigned align) {
    auto *srcOperand = store->getOperand(0);
    auto *srcType = srcOperand->getType();
    if (srcType->isPointerTy() || srcType->isIntegerTy()) return store;
    auto *ptrOperand = store->getPointerOperand();
    Type* storeType = targetType;
    if (!storeType) {
        if (auto *allocaInst = findOriginalAlloca(ptrOperand))
            storeType = allocaInst->getAllocatedType();
    } else {
        if (auto *arrayType = dyn_cast<ArrayType>(storeType))
            storeType = arrayType->getElementType();
        store->setAlignment(align ? Align(align) : getAlign(storeType));
        // errs() << "[repair store] " << *store << "->";
        if (auto *castValue = insertCast(srcOperand, storeType, store))
            store->setOperand(0, castValue);
        // errs() << *store << '\n';
    }
    return store;
}

Instruction* ChangeVariableDecl::repairUnaryOp(UnaryOperator* unaOp) {
    auto *operand = unaOp->getOperand(0);
    if (operand->getType() != unaOp->getType()) {
        // errs() << "[repair unary op] " << *unaOp << "->";
        IRBuilder<> builder(unaOp);
        auto *newUnaOp = builder.CreateUnOp(unaOp->getOpcode(), operand);
        newUnaOp->takeName(unaOp);
        if (auto *newUnaOpInst = dyn_cast<Instruction>(newUnaOp)) {
            newUnaOpInst->setDebugLoc(unaOp->getDebugLoc());
        }
        unaOp->replaceAllUsesWith(newUnaOp);
        instToErase.insert(unaOp);
        // errs() << *newUnaOp << '\n';
        return dyn_cast<Instruction>(newUnaOp);
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairBinaryOp(BinaryOperator* binOp) {
    SmallVector<Value*, 2> newOperands;
    auto *highestType = findHighestPrecisionType(binOp);

    bool operandsChanged = false;
    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            // 将原始cast指令添加到待删除列表，消除冗余
            instToErase.insert(castInst);
            operandsChanged = true;
        }
        // type of src operands not match
        if (operand->getType() != highestType) {
            if (auto *newOperand = insertCast(operand, highestType, binOp)) {
                operand = newOperand;
                operandsChanged = true;
            }
        }
        newOperands.push_back(operand);
    }

    if (binOp->getType() != highestType) {
        IRBuilder<> builder(binOp);
        auto *newBinOp = builder.CreateBinOp(binOp->getOpcode(), 
                                             newOperands[0], 
                                             newOperands[1]);
        newBinOp->takeName(binOp);
        if (auto *newBinOpInst = dyn_cast<Instruction>(newBinOp)) {
            newBinOpInst->setDebugLoc(binOp->getDebugLoc());
            replaceMultiOperandInst(binOp, newBinOpInst);
        }
        // errs() << "[repair binop] " << *binOp << "->";
        binOp = dyn_cast<BinaryOperator>(newBinOp);
    } else if (operandsChanged) {
        for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
            binOp->setOperand(i, newOperands[i]);
        }
    } else {
        // 操作数已经是正确类型，不需要修改，直接返回nullptr避免递归处理
        // errs() << "[binop already correct] " << *binOp << '\n';
        return nullptr;
    }
    // errs() << *binOp << '\n';
    return binOp;
}

Instruction* ChangeVariableDecl::repairFCmpInst(FCmpInst* fcmp) {
    SmallVector<Value*, 2> newOperands;
    auto *highestType = findHighestPrecisionType(fcmp);
    
    for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
        auto *operand = fcmp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            // 将原始cast指令添加到待删除列表，消除冗余
            instToErase.insert(castInst);
        }
        if (operand->getType() != highestType) {
            if (auto *newOperand = insertCast(operand, highestType, fcmp)) {
                operand = newOperand;
            }
        }
        newOperands.push_back(operand);
    }

    if (fcmp->getType() != Type::getInt1Ty(fcmp->getContext())) {
        IRBuilder<> builder(fcmp);
        auto *newFcmp = builder.CreateFCmp(fcmp->getPredicate(), 
                                            newOperands[0], 
                                            newOperands[1]);
        newFcmp->takeName(fcmp);
        if (auto *newFcmpInst = dyn_cast<Instruction>(newFcmp)) {
            newFcmpInst->setDebugLoc(fcmp->getDebugLoc());
            replaceMultiOperandInst(fcmp, newFcmpInst);
        }
        // errs() << "[repair FCmp] " << *fcmp << "->";
        fcmp = dyn_cast<FCmpInst>(newFcmp);
    } else {
        for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
            fcmp->setOperand(i, newOperands[i]);
        }
    }
    // errs() << *fcmp << '\n';
    return fcmp;
}

Instruction* ChangeVariableDecl::repairCallInst(CallInst* call) {
    auto *callee = call->getCalledFunction();
    if (!callee) return nullptr;
    
    // 处理llvm.fmuladd函数调用
    if (callee->getName().starts_with("llvm.fmuladd")) {
        // 获取当前fmuladd指令的类型
        Type* oldType = call->getType();
        Type* highestType = nullptr;
        SmallVector<Value*, 3> newOperands;

        // 分析每个操作数的原始类型以及最高类型
        for (unsigned i = 0; i < 3; ++i) {
            Value* operand = call->getArgOperand(i);
            Value* sourceOperand = operand;
            Type* sourceType = operand->getType();
            // if typecast, use src value
            if (auto castInst = dyn_cast<CastInst>(operand)) {
                sourceType = castInst->getSrcTy();
                sourceOperand = castInst->getOperand(0);
                // 将原始cast指令添加到待删除列表，消除冗余
                instToErase.insert(castInst);
            }
            newOperands.push_back(sourceOperand);
            // 浮点常数不锁定类型
            if (isa<Constant>(sourceOperand) && sourceType->isFloatingPointTy()) continue;
            
            // 更新最高精度类型
            if (sourceType->isFloatingPointTy()) {
                if (!highestType || sourceType->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
                    highestType = sourceType;
                }
            }
        }

        if (!highestType)
            highestType = oldType;

        // 更新newOperands并插入必要cast
        bool operandsChanged = false;
        for (unsigned i = 0; i < 3; ++i) {
            Value* source = newOperands[i];
            if (source->getType() != highestType) {
                if (auto* casted = insertCast(source, highestType, call)) {
                    newOperands[i] = casted;
                    operandsChanged = true;
                }
            }
        }

        // 检查操作数是否需要转换（无论当前类型是否与最高精度类型相同）
        bool needsRepair = highestType != oldType || operandsChanged;
        SmallVector<Value*, 3> castedOperands;
        
        // 如果需要修复（最高精度类型与当前类型不同，或操作数类型不一致）
        if (needsRepair) {
            // 构建新的函数名
            std::string newFuncName = "llvm.fmuladd.";
            if (highestType->isHalfTy()) {
                newFuncName += "f16";
            } else if (highestType->isFloatTy()) {
                newFuncName += "f32";
            } else if (highestType->isDoubleTy()) {
                newFuncName += "f64";
            } else if (highestType->isX86_FP80Ty()) {
                newFuncName += "f80";
            } else if (highestType->isFP128Ty()) {
                newFuncName += "f128";
            } else {
                // 不支持的类型，跳过
                return nullptr;
            }

            // 获取或创建新的函数
            Function* newFunc = call->getModule()->getFunction(newFuncName);
            if (!newFunc) {
                FunctionType* newFuncType = FunctionType::get(
                    highestType, 
                    {highestType, highestType, highestType}, 
                    false
                );
                newFunc = Function::Create(
                    newFuncType, 
                    GlobalValue::ExternalLinkage, 
                    newFuncName, 
                    call->getModule()
                );
            }

            // 确保所有操作数都是最高精度类型
            for (unsigned i = 0; i < 3; ++i) {
                Value* source = newOperands[i];
                if (source->getType() != highestType) {
                    if (Value* casted = insertCast(source, highestType, call)) {
                        castedOperands.push_back(casted);
                    } else {
                        // 转换失败，跳过
                        return nullptr;
                    }
                } else {
                    castedOperands.push_back(source);
                }
            }

            // 创建新的call指令
            CallInst* newCall = CallInst::Create(newFunc, castedOperands, "", call->getIterator());
            newCall->setDebugLoc(call->getDebugLoc());
            newCall->takeName(call);
            replaceMultiOperandInst(call, newCall);
            // errs() << "[repair fmuladd] " << *call << "->";
            call = newCall;
        } else {
            // 操作数已经是正确类型，不需要修改，直接返回nullptr避免递归处理
            // errs() << "[fmuladd already correct] " << *call << '\n';
            return nullptr;
        }
        // errs() << *call << '\n';
        return call;
    }
    
    // 处理其他函数调用
    bool modified = false;
    unsigned numFixedArgs = callee->getFunctionType()->getNumParams();
    // errs() << "[repair call] " << *call << "->";
    // 遍历调用的所有参数
    for (unsigned i = 0; i < call->arg_size(); ++i) {
        Value *arg = call->getArgOperand(i);
        Type *targetType = nullptr;

        // 情况 A: 固定参数部分 (根据函数声明确定目标类型)
        if (i < numFixedArgs) {
            targetType = callee->getFunctionType()->getParamType(i);
        } 
        // 情况 B: 变长参数部分 (根据原始类型恢复)
        else if (callee->isVarArg()) {
            auto it = varargCallOriginalTypes.find(call);
            if (it != varargCallOriginalTypes.end()) {
                // 使用原始IR中的类型作为目标类型
                targetType = it->second[i];
            } else {
                // 规则：float 必须提升为 double
                if (arg->getType()->isFloatTy() || arg->getType()->isHalfTy()) {
                    targetType = Type::getDoubleTy(call->getContext());
                }
                // 注意：这里也可以添加整数提升 (char/short -> int)，视需求而定
            }
        }

        // 如果确定了目标类型，且与当前类型不符，则插入转换
        if (targetType && arg->getType() != targetType) {
            if (auto *castValue = insertCast(arg, targetType, call)) {
                call->setArgOperand(i, castValue); // 替换参数
                modified = true;
            } else {
                // errs() << "[repair call] Failed to cast arg" << i << " to type " << *targetType << '\n';
            }
        }
    }

    if (modified) {
        // errs() << *call << '\n';
        return call;
    }
    // errs() << '\n';
    return nullptr;
}

Instruction* ChangeVariableDecl::repairPHINode(PHINode* phi) {
    auto *highestType = findHighestPrecisionType(phi);
    bool modified = false;

    // 检查是否需要创建新的 PHINode
    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        Value* oldVal = phi->getIncomingValue(i);
        if (oldVal->getType() != highestType) {
            modified = true;
            break;
        }
    }

    if (modified) {
        // 创建新的 PHINode，类型为 highestType
        auto *newPhi = PHINode::Create(highestType, phi->getNumIncomingValues(), "", phi->getIterator());
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
            auto *oldVal = phi->getIncomingValue(i);
            auto *newVal = oldVal;
            // pay attention: insertCast shouldn't at 'phi', will cause 'Instruction does not dominate all uses!'
            if (oldVal->getType() != highestType) {
                auto *incomingBlock = phi->getIncomingBlock(i);
                auto *insertBefore = incomingBlock->getTerminator();
                newVal = insertCast(oldVal, highestType, insertBefore);
            }

            newPhi->addIncoming(newVal, phi->getIncomingBlock(i));
        }
        phi->replaceAllUsesWith(newPhi);
        instToErase.insert(phi);
        // errs() << "[repair phi] " << *phi << "->" << *newPhi << '\n';
        return dyn_cast<Instruction>(newPhi);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairGEPInst(GetElementPtrInst* gep, Type* targetType) {
    if (!targetType) return nullptr;
    auto *srcType = gep->getSourceElementType();
    // 多级指针的情形，保持targetType状态，返回原指令，直到访问到最低一级的元素类型，将其转换为targetType
    if (srcType->isPointerTy()) return gep;

    // 提取 GEP 指令的索引
    SmallVector<Value*, 4> indices;
    for (auto& index : gep->indices()) {
        indices.push_back(index.get());
    }
    auto *newSrcType = replaceArrayBaseType(srcType, stripArrayType(targetType));

    // 创建新的 GEP 指令
    auto* newGEP = GetElementPtrInst::CreateInBounds(
        newSrcType,
        gep->getPointerOperand(),
        indices,
        gep->getName(),
        gep->getIterator()
    );

    // 用新的 GEP 指令替换旧的 GEP 指令
    gep->replaceAllUsesWith(newGEP);
    instToErase.insert(gep);
    // errs() << "[repair GEP] " << *gep << "->" << *newGEP << '\n';

    return newGEP;
}

Constant* ChangeVariableDecl::repairGEPConstantExpr(ConstantExpr *constant, Type* targetType) {
    if (!targetType || constant->getOpcode() != Instruction::GetElementPtr)
        return nullptr;

    auto *gepOp = cast<GEPOperator>(constant);
    auto *srcType = gepOp->getSourceElementType();
    if (srcType->isPointerTy())
        return constant;

    auto *ptrOperand = dyn_cast<Constant>(constant->getOperand(0));
    if (!ptrOperand)
        return nullptr;

    SmallVector<Value*, 4> indices;
    for (auto index = gepOp->idx_begin(); index != gepOp->idx_end(); ++index)
        indices.push_back(index->get());

    auto *newSrcType = replaceArrayBaseType(srcType, stripArrayType(targetType));
    auto *newConstant = ConstantExpr::getGetElementPtr(
        newSrcType,
        ptrOperand,
        indices,
        gepOp->isInBounds()
    );

    if (newConstant != constant)
        constant->replaceAllUsesWith(newConstant);

    return newConstant;
}

Instruction* ChangeVariableDecl::repairBitCastInst(BitCastInst* bitCast) {
    Value* srcValue = bitCast->getOperand(0);
    Type* srcType = srcValue->getType();
    Type* destType = bitCast->getDestTy();

    // 仅处理源类型为浮点类型的情况
    if (!srcType->isFloatingPointTy()) {
        return nullptr;
    }

    // 确定与目标类型位宽相同的浮点类型
    LLVMContext& context = bitCast->getContext();
    Type* targetFloatType;
    unsigned destBitWidth = destType->getPrimitiveSizeInBits();
    switch (destBitWidth) {
        case 16:
            targetFloatType = Type::getHalfTy(context);
            break;
        case 32:
            targetFloatType = Type::getFloatTy(context);
            break;
        case 64:
            targetFloatType = Type::getDoubleTy(context);
            break;
        case 128:
            targetFloatType = Type::getFP128Ty(context);
            break;
        default:
            errs() << "Unsupported bit width for floating point extension: " << destBitWidth << "\n";
            return nullptr;
    }

    Value* newSrcValue = insertCast(srcValue, targetFloatType, bitCast);
    if (!newSrcValue) {
        // errs() << "Failed to cast source value in BitCastInst: " << *bitCast << "\n";
        return nullptr;
    }

    BitCastInst* newBitCast =             new BitCastInst(newSrcValue, destType, bitCast->getName(), bitCast->getIterator());
    bitCast->replaceAllUsesWith(newBitCast);
    instToErase.insert(bitCast);
    // errs() << "[repair bitcast] " << *bitCast << "->" << *newBitCast << '\n';

    return newBitCast;
}

// 修复SIToFP和UIToFP指令的目标类型
Instruction* ChangeVariableDecl::repairIntToFP(Instruction* castInst) {    
    Value* sourceOperand = castInst->getOperand(0);
    Type* targetType = nullptr;
    
    // 检查源操作数是否来自转换指令
    if (auto *srcCast = dyn_cast<CastInst>(sourceOperand)) {
        bool castFromFP = (srcCast->getOpcode() == CastInst::FPExt || 
                           srcCast->getOpcode() == CastInst::FPTrunc || 
                           srcCast->getOpcode() == CastInst::FPToSI || 
                           srcCast->getOpcode() == CastInst::FPToUI);
        if (castFromFP)
            targetType = srcCast->getSrcTy();
    }
    
    // 如果找到合适的目标类型，且与当前类型不同，则修复
    if (targetType && targetType != castInst->getType()) {
        // errs() << "[repair IntToFP] " << *castInst << "->";
        
        // 创建新的转换指令
        IRBuilder<> builder(castInst);
        Value* newCast = nullptr;
        if (castInst->getOpcode() == CastInst::SIToFP)
            newCast = builder.CreateSIToFP(sourceOperand, targetType, castInst->getName());
        else
            newCast = builder.CreateUIToFP(sourceOperand, targetType, castInst->getName());
        auto *newCastInst = dyn_cast<Instruction>(newCast);
        newCastInst->setDebugLoc(castInst->getDebugLoc());
        
        castInst->replaceAllUsesWith(newCast);
        instToErase.insert(castInst);
        // errs() << *newCast << '\n';
        return newCastInst;
    }
    
    return nullptr;
}

// Make inst operands type consistent
void ChangeVariableDecl::repairUserType(Value* origin, User *user, Type* targetType, unsigned align) {
    if (auto *constant = dyn_cast<ConstantExpr>(user)) {
        auto *newConstant = repairGEPConstantExpr(constant, targetType);
        if (newConstant && newConstant != constant) {
            Type *nextTargetType = targetType;
            if (auto *gepOp = dyn_cast<GEPOperator>(newConstant))
                nextTargetType = gepOp->getResultElementType();

            SmallVector<User*> users;
            for (auto *constantUser : newConstant->users())
                users.push_back(constantUser);
            for (auto *constantUser : users)
                repairUserType(newConstant, constantUser, nextTargetType, align);
        }
        return;
    }

    if (!isa<Instruction>(user)) {
        // errs() << "[not an instruction]\n";
        return;
    }

    auto *inst = cast<Instruction>(user);
    Instruction *newInst = nullptr;
    // errs() << "[checking user] " << *inst << "\n";

    if (auto store = dyn_cast<StoreInst>(inst)) {
        auto *srcOp = store->getValueOperand();
        auto *dstOp = store->getPointerOperand();
        if (origin == dstOp) {
            // store转换为dstOp类型
            newInst = repairStoreInst(store, targetType, align);
        } else if (origin == srcOp) {
            // srcOp转换为store类型
            auto *dstType = getBasicType(dstOp);
            if (auto *castValue = insertCast(srcOp, dstType, store)) {
                store->setOperand(0, castValue);
                newInst = store;
            }
        }
    }
    else if (auto ret = dyn_cast<ReturnInst>(inst))
        newInst = repairReturnInst(ret);
    else if (auto load = dyn_cast<LoadInst>(inst))
        newInst = repairLoadInst(load, targetType, align);
    else if (auto *unaOp = dyn_cast<UnaryOperator>(inst))
        newInst = repairUnaryOp(unaOp);
    else if (auto *binOp = dyn_cast<BinaryOperator>(inst))
        newInst = repairBinaryOp(binOp);
    else if (auto *fcmp = dyn_cast<FCmpInst>(inst))
        newInst = repairFCmpInst(fcmp);
    else if (auto *call = dyn_cast<CallInst>(inst))
        newInst = repairCallInst(call);
    else if (auto *phi = dyn_cast<PHINode>(inst))
        newInst = repairPHINode(phi);
    else if (auto *gep = dyn_cast<GetElementPtrInst>(inst))
        newInst = repairGEPInst(gep, targetType);
    else if (auto *castInst = dyn_cast<SIToFPInst>(inst))
        newInst = repairIntToFP(castInst);
    else if (auto *castInst = dyn_cast<UIToFPInst>(inst))
        newInst = repairIntToFP(castInst);

    if (isa<FPToSIInst>(inst) || isa<FPToUIInst>(inst))
        newInst = inst;

    if (newInst) {
        Type *nextTargetType = targetType;
        if (auto *gep = dyn_cast<GetElementPtrInst>(newInst))
            nextTargetType = gep->getResultElementType();

        // 防止修改迭代器导致无限递归
        SmallVector<User*> users;
        for (auto *user : newInst->users())
            users.push_back(user);
        for (auto *user : users)
            repairUserType(newInst, user, nextTargetType);
        users.clear();
    }
}

// Change global & static & local static to target type
void ChangeVariableDecl::changeGlobalVariable(GlobalVariable *GV, Type* targetType) {
    if (!targetType || !GV->hasInitializer()) return;
    if (GV->getValueType()->isArrayTy())
        targetType = replaceArrayBaseType(GV->getValueType(), stripArrayType(targetType));
    if (targetType == GV->getValueType()) return;

    auto *initializer = GV->getInitializer();
    auto *newInitializer = GV->getValueType()->isPointerTy() ? 
                            initializer : changeGVInitializer(initializer, targetType);
    if (!newInitializer) return;
    GlobalVariable *newGV = GV;
    if (newInitializer != initializer) {
        // errs() << "[change GV] " << *GV << " -> ";
        newGV = new GlobalVariable(
                    *GV->getParent(),
                    targetType,
                    GV->isConstant(),
                    GV->getLinkage(),
                    newInitializer,
                    GV->getName()
                );
        newGV->copyAttributesFrom(GV);
        // TODO: 复制metadata，但metadata中的类型信息未改为targetType
        for (unsigned i = 0; i < GV->getNumOperands(); ++i) {
            if (GV->hasMetadata(i)) {
                newGV->setMetadata(i, GV->getMetadata(i));
            }
        }
        newGV->setThreadLocalMode(GV->getThreadLocalMode());
        newGV->setExternallyInitialized(GV->isExternallyInitialized());
        newGV->setAlignment(getAlign(targetType));
        GV->replaceAllUsesWith(newGV);
        // errs() << *newGV << '\n';
    }

    // Extract base type from targetType and store in globalBaseTypeMap
    Type *baseType = targetType;
    // 递归获取数组的基类型
    while (baseType->isArrayTy()) {
        baseType = baseType->getArrayElementType();
    }
    globalBaseTypeMap[newGV] = baseType;
    // errs() << "[GV base type] " << newGV->getName() << " -> " << *baseType << '\n';

    SmallVector<User*> users;
    for (auto *user : newGV->users())
        users.push_back(user);
    for (auto *user : users)
        repairUserType(newGV, user, targetType);

    if (initializer != newInitializer)
        GVToErase.insert(GV);
}

Constant* ChangeVariableDecl::changeGVInitializer(Constant *initializer, Type* targetType) {
    if (isa<ConstantAggregateZero>(initializer)) {
        // 如果原初始化器是zeroinitializer，创建新类型的zeroinitializer
        return ConstantAggregateZero::get(targetType);
    } else if (isa<ConstantFP>(initializer)) {
        // TODO: 该语句并不生效，为什么？
        // newInitializer = ConstantFP::get(targetType, cfp->getValueAPF());
        return ConstantExpr::getCast(CastInst::FPExt, initializer, targetType);
    } else if (isa<ConstantDataArray>(initializer)) {
        // 处理ConstantDataArray初始化器（如字符串常量）
        auto *targetArrayType = dyn_cast<ArrayType>(targetType);
        if (!targetArrayType)
            report_fatal_error("ConstantDataArray: targetType is not ArrayType");
        auto *targetElementType = targetArrayType->getElementType();
        auto *dataArr = dyn_cast<ConstantDataArray>(initializer);
        // 如果元素类型相同，直接返回原初始化器
        if (dataArr->getElementType() == targetElementType)
            return initializer;
        
        unsigned numElements = dataArr->getNumElements();
        SmallVector<Constant*, 8> newElements;
        for (unsigned i = 0; i < numElements; ++i) {
            auto *elem = dataArr->getElementAsConstant(i);
            auto *newElem = changeGVInitializer(elem, targetElementType);
            if (!newElem)
                report_fatal_error("ConstantDataArray: failed to convert element");
            newElements.push_back(newElem);
        }
        return ConstantArray::get(targetArrayType, newElements);
    } else if (isa<ConstantArray>(initializer)) {
        // 处理ConstantArray初始化器（如结构体中的数组）
        auto *targetArrayType = dyn_cast<ArrayType>(targetType);
        if (!targetArrayType)
            return initializer;
        
        auto *targetElementType = targetArrayType->getElementType();
        auto *array = dyn_cast<ConstantArray>(initializer);
        // 如果元素类型相同，直接返回原初始化器
        if (array->getType()->getElementType() == targetElementType)
            return initializer;
        
        unsigned numElements = array->getNumOperands();
        SmallVector<Constant*, 8> newElements;
        for (unsigned i = 0; i < numElements; ++i) {
            auto *elem = array->getOperand(i);
            auto *newElem = changeGVInitializer(elem, targetElementType);
            if (!newElem)
                return initializer;
            newElements.push_back(newElem);
        }
        return ConstantArray::get(targetArrayType, newElements);
    } else if (isa<ConstantStruct>(initializer)) {
        // 处理ConstantStruct初始化器（如结构体本身）
        auto *targetStructType = dyn_cast<StructType>(targetType);
        if (!targetStructType)
            return initializer;
        
        auto *structInit = dyn_cast<ConstantStruct>(initializer);
        // 如果成员数量相同，直接返回原初始化器
        if (structInit->getNumOperands() != targetStructType->getNumElements())
            return initializer;
        
        SmallVector<Constant*, 8> newElements;
        for (unsigned i = 0; i < structInit->getNumOperands(); ++i) {
            auto *elem = structInit->getOperand(i);
            auto *elemTargetType = targetStructType->getElementType(i);
            auto *newElem = changeGVInitializer(elem, elemTargetType);
            if (!newElem)
                newElements.push_back(elem);
            else
                newElements.push_back(newElem);
        }
        return ConstantStruct::get(targetStructType, newElements);
    } 
    // errs() << "Ignored intializer: " << *initializer << '\n';
    return nullptr;
}


// Remove redundant & Replace wrong fpext/fptrunc insts
void llvm::ChangeVariableDecl::repairCasts(Function &F)
{
    IRBuilder<> builder(F.getContext());

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *castInst = dyn_cast<CastInst>(&I)) {
                // 1. 首先处理未使用的cast
                if (castInst->use_empty()) {
                    // errs() << "[remove unused cast] " << *castInst << " in " << F.getName() << '\n';
                    instToErase.insert(castInst);
                    continue;
                }
                
                // 2. 处理相同类型的冗余cast
                if (castInst->getSrcTy() == castInst->getDestTy()) {
                    // errs() << "[remove redundant cast] " << *castInst << '\n';
                    castInst->replaceAllUsesWith(castInst->getOperand(0));
                    instToErase.insert(castInst);
                    continue;
                }
                
                // 3. 处理连续cast调用链
                bool hasContinuousCast = false;
                bool hasIrreversibleCast = false;
                Value *current = castInst->getOperand(0);
                std::vector<CastInst*> castChain;
                
                // 跟踪cast链，找到最初的源操作数，同时记录链中的所有cast指令
                while (auto *chainCast = dyn_cast<CastInst>(current)) {
                    // 非浮点间转换不可作为简化链的一部分
                    switch (chainCast->getOpcode()) {
                        case CastInst::FPToSI:
                        case CastInst::FPToUI:
                        case CastInst::SIToFP:
                        case CastInst::UIToFP:
                        case CastInst::Trunc:
                        case CastInst::ZExt:
                        case CastInst::SExt:
                        case CastInst::IntToPtr:
                        case CastInst::PtrToInt:
                            hasIrreversibleCast = true;
                            break;
                        default:
                            break;
                    }
                    castChain.push_back(chainCast);
                    current = chainCast->getOperand(0);
                    hasContinuousCast = true;
                }
                
                if (hasContinuousCast && !hasIrreversibleCast) {
                    // 使用insertCast函数创建从最初源操作数到最终目标类型的单个cast指令
                    if (Value *newCast = insertCast(current, castInst->getDestTy(), castInst)) {
                        // 如果创建了新的cast指令且与原指令不同，则替换原指令的所有使用
                        if (newCast != castInst) {
                            castInst->replaceAllUsesWith(newCast);
                            
                            // 将原cast指令和链中的所有cast指令添加到删除列表
                            instToErase.insert(castInst);
                            for (auto *chainCast : castChain) {
                                // 只删除那些只有当前cast链使用的中间cast指令
                                if (chainCast->use_empty() || std::all_of(chainCast->user_begin(), chainCast->user_end(),
                                    [&](User *U) {
                                        return dyn_cast<CastInst>(U) && (U == castInst || std::find(castChain.begin(), castChain.end(), U) != castChain.end());
                                    })) {
                                    instToErase.insert(chainCast);
                                }
                            }
                            
                            // errs() << "[replace cast chain] " << *castInst << "->" << *newCast << '\n';
                            continue;
                        }
                    }
                }
                
                // 4. 修复错误的fpext/fptrunc指令
                if (isa<FPExtInst>(castInst) || isa<FPTruncInst>(castInst)) {
                    auto *srcType = castInst->getSrcTy();
                    auto *destType = castInst->getDestTy();
                    
                    // 如果目标类型精度低于源类型，且当前是FPExt，则改为FPTrunc
                    if (destType->getPrimitiveSizeInBits() < srcType->getPrimitiveSizeInBits() && 
                        isa<FPExtInst>(castInst)) {
                        builder.SetInsertPoint(castInst);
                        auto *newCast = builder.CreateFPTrunc(castInst->getOperand(0), destType);
                        castInst->replaceAllUsesWith(newCast);
                        instToErase.insert(castInst);
                    }
                    // 如果目标类型精度高于源类型，且当前是FPTrunc，则改为FPExt
                    else if (destType->getPrimitiveSizeInBits() > srcType->getPrimitiveSizeInBits() && 
                             isa<FPTruncInst>(castInst)) {
                        builder.SetInsertPoint(castInst);
                        auto *newCast = builder.CreateFPExt(castInst->getOperand(0), destType);
                        castInst->replaceAllUsesWith(newCast);
                        instToErase.insert(castInst);
                    }
                    continue;
                }

                // 5. 处理BitCast指令
                if (auto *bitCast = dyn_cast<BitCastInst>(&I)) {
                    repairBitCastInst(bitCast);
                    continue;
                }
            }
        }
    }
}

// find the original AllocaInst of given inst
AllocaInst* ChangeVariableDecl::findOriginalAlloca(Value* value) {
    while (true) {
        if (auto *alloca = dyn_cast<AllocaInst>(value)) {
            return alloca;
        }
        if (auto *load = dyn_cast<LoadInst>(value)) {
            value = load->getPointerOperand();
        }
        else if (auto *gep = dyn_cast<GetElementPtrInst>(value)) {
            value = gep->getPointerOperand();
        } else {
            break;
        }
    }
    return nullptr;
}

// Translate Perci-Tuner supported typename into llvm type
Type* ChangeVariableDecl::typeNameToBasicType(string typeName, LLVMContext& context)
{
    size_t constPos;
    while ((constPos = typeName.find("const")) != string::npos) {
        typeName.erase(constPos, 5);  // Remove 'const'
    }

    Type* targetType = nullptr;
    // if (typeName.find("__bf16") != string::npos) {
    //     targetType = Type::getBFloatTy(context);
    // }
    // else 
    if (typeName.find("__fp16") != string::npos) {
        targetType = Type::getHalfTy(context);
    } else if (typeName.find("float") != string::npos) {
        targetType = Type::getFloatTy(context);
    } else if (typeName.find("long double") != string::npos) {
        targetType = Type::getFP128Ty(context);
    } else if (typeName.find("double") != string::npos) {
        targetType = Type::getDoubleTy(context);
    } else {
        return nullptr;
    }
    // 为了保证上下文可获知基类型，此处不需要封装为ptr类型，而是在处理位置特殊处理
    // if (typeName.find("*") != string::npos)
    //     targetType = PointerType::get(targetType, 0);
    
    if (typeName.find("[") == string::npos) return targetType;

    // Extract the size of the array
    size_t openBracket = typeName.find('[');
    size_t closeBracket = typeName.find(']');
    if (openBracket != string::npos && closeBracket != string::npos) {
        string sizeStr = typeName.substr(openBracket + 1, closeBracket - openBracket - 1);
        int arraySize = std::stoi(sizeStr);
        ArrayType *arrayType = ArrayType::get(targetType, arraySize);
        return arrayType;
    }
    return nullptr;
}

void ChangeVariableDecl::runOnFunction(Function &F) {
    auto &context = F.getContext();

    json::Value* localVal = config.getAsObject()->get("local");
    if (!localVal || localVal->kind() == json::Value::Null) return;
    auto* localObj = localVal->getAsObject();
    if (!localObj) return;

    json::Value* funcConfig = localObj->get(demangle(F.getName()));
    if (!funcConfig || funcConfig->kind() == json::Value::Null) return;

    // 检查是否需要修改函数返回值类型
    auto* funcConfigArr = funcConfig->getAsArray();
    if (!funcConfigArr) return;
    for (auto &configEntry : *funcConfigArr) {
        auto* entryObj = configEntry.getAsObject();
        if (!entryObj) continue;
        auto nameOpt = entryObj->getString("name");
        if (nameOpt && *nameOpt == "ret.") {
            auto srcTypeName = parseTypeName(F.getFunctionType()->getReturnType());
            auto typeOpt = entryObj->getString("type");
            std::string targetTypeName = typeOpt ? typeOpt->str() : "";
            // TODO: 暂不处理指针类型返回值
            if (targetTypeName.find('*') != string::npos) continue;
            auto *targetType = typeNameToBasicType(targetTypeName, context);
            if (targetType == nullptr) continue;
            
            // errs() << "[change return type] " << F.getName() << ": " << srcTypeName << "->" << *targetType << "\n";
            funcRetTypeMap[&F] = targetType;
            break;
        }
    }

    // Check if decl inst need to be changed
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
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
                // local static
                if (GV && GV->hasInternalLinkage()) {
                    auto DL = I.getDebugLoc();
                    if (DL) {
                        auto *localScope = llvm::dyn_cast<llvm::DILocalScope>(DL.getScope());
                        string fileName = localScope->getNonLexicalBlockFileScope()->getFile()->getFilename().str();

                        std::string key = fileName + ":" + std::to_string(DL.getLine()) + ":" + std::to_string(DL.getCol());
                        auto it = configMap.find(key);
                        if (it == configMap.end()) continue;

                        auto &configEntry = it->second;
                        auto* entryObj = configEntry.getAsObject();
                        auto typeOpt = entryObj->getString("type");
                        std::string typeName = typeOpt ? typeOpt->str() : "";
                        auto *targetType = typeNameToBasicType(typeName, context);
                        changeGlobalVariable(GV, targetType);
                    }
                }
            }
            // 适用clang情况
            else if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                    auto varName = DV->getName();
                    auto fileName = DV->getFile()->getFilename().str();
                    auto loc = DDI->getDebugLoc();

                    std::string key = fileName + ":" + std::to_string(loc.getLine()) + ":" + std::to_string(loc.getCol());
                    auto it = configMap.find(key);
                    if (it == configMap.end()) continue;
                    auto &configEntry = it->second;
                    auto* entryObj = configEntry.getAsObject();
                    auto typeOpt = entryObj->getString("type");
                    std::string typeName = typeOpt ? typeOpt->str() : "";
                    auto srcTypeName = parseDITypeName(DV->getType());
                    // if (typeName == srcTypeName) continue;
                    auto *targetType = typeNameToBasicType(typeName, context);
                    if (targetType == nullptr) continue;
                    
                    if (entryObj->find("isParam") != entryObj->end() && entryObj->getBoolean("isParam").value_or(false)) {
                        unsigned argIndex = 0;
                        for (auto argIt = F.arg_begin(); argIt != F.arg_end(); ++argIt, ++argIndex) {
                            if (argIt->getName() == varName) {
                                // errs() << "[change arg] " << argIt->getName() << ": " << srcTypeName << "->" << *targetType << '\n';
                                funcDeclTypeMap[&F][argIndex] = targetType;
                                break;
                            }
                        }
                    }
                    changeAllocaInst(dyn_cast<AllocaInst>(DDI->getAddress()), targetType);
                }
            }
            // 适用flang-new情况 + LLVM19后
            else if (auto *alloca = dyn_cast<AllocaInst>(&I)) {
                unsigned lineNo = 0, colNo = 0;
                string fileName = "", varName = "", srcTypeName = "";
                if (auto *N = alloca->getMetadata("dbg")) {
                    if (auto *Loc = dyn_cast<DILocation>(N)) {
                        lineNo = Loc->getLine();
                        colNo = Loc->getColumn();
                        if (auto *Scope = Loc->getScope()) {
                            fileName = Scope->getFilename().str();
                        }
                        if (alloca->hasName()) varName = alloca->getName().str();
                        srcTypeName = parseTypeName(alloca->getAllocatedType());
                    }

                    std::string key = fileName + ":" + std::to_string(lineNo) + ":" + std::to_string(colNo);
                    auto it = configMap.find(key);
                    if (it == configMap.end()) continue;
                    auto &configEntry = it->second;
                    auto* entryObj = configEntry.getAsObject();
                    auto typeOpt = entryObj->getString("type");
                    std::string typeName = typeOpt ? typeOpt->str() : "";
                    // if (typeName == srcTypeName) continue;
                    auto *targetType = typeNameToBasicType(typeName, context);
                    if (targetType == nullptr) continue;

                    changeAllocaInst(alloca, targetType);
                }
            }
        }
    }

    for (auto *newAlloca : newAllocas)
        repairAllocaUsers(newAlloca);
    newAllocas.clear();

    removeUnusedReplacements();

    for (auto *inst : instToErase) {
        inst->eraseFromParent();
    }
    instToErase.clear();
}

void ChangeVariableDecl::changeStructDeclarations(Module &module) {
    json::Value* structsVal = config.getAsObject()->get("structs");
    if (!structsVal || structsVal->kind() == json::Value::Null) return;
    auto* structsObj = structsVal->getAsObject();
    if (!structsObj) return;

    auto &context = module.getContext();
    // Iterate all struct decls in the module
    for (auto *sty : module.getIdentifiedStructTypes()) {
        StringRef rawName = sty->getName();
        StringRef structName = rawName.starts_with("struct.") ? rawName.substr(7) : rawName;
        json::Value* structEntry = structsObj->get(structName);
        if (!structEntry || structEntry->kind() == json::Value::Null) continue;
        auto* membersArr = structEntry->getAsArray();
        if (!membersArr) continue;
        SmallVector<Type*, 4> memberNewTypes, memberNewBaseTypes;  // 成员的完整类型和基类型
        bool needsUpdate = false;

        // Iterate original struct members
        for (unsigned i = 0; i < sty->getNumElements(); ++i) {
            auto *oldType = sty->getElementType(i);
        
            auto* memberObj = (*membersArr)[i].getAsObject();
            if (!memberObj) continue;
            auto typeOpt = memberObj->getString("type");
            std::string typeStr = typeOpt ? typeOpt->str() : "";
            auto *targetType = typeNameToBasicType(typeStr, context);

            if (targetType && targetType != oldType) {
                if (oldType->isPointerTy())
                    oldType = PointerType::get(context, 0);
                else {
                    // errs() << "[change struct member] " << structName << " member " << i << ": " << *oldType << "->" << *targetType << '\n';
                    oldType = targetType;
                }
                needsUpdate = true;
            }
            memberNewBaseTypes.push_back(targetType);
            memberNewTypes.push_back(oldType);
        }
        if (!needsUpdate) continue;
        
        sty->setBody(memberNewTypes, sty->isPacked());

        // TODO：尚不支持强制对齐方式#pragma pack(n)
        unsigned maxAlign = 0;
        SmallVector<unsigned, 4> maxAlignTillNow;  // 后续Load/Store指令使用的对齐长度遵循“此前最长”这一规则
        const auto &DL = module.getDataLayout();
        for (unsigned i = 0; i < sty->getNumElements(); ++i) {
            auto *elemType = sty->getElementType(i);
            unsigned align = DL.getABITypeAlign(elemType).value();
            if (align > maxAlign) {
                maxAlign = align;
            }
            maxAlignTillNow.push_back(maxAlign);
        }
        maxAlignTillNow[0] = maxAlign;  // IMPORTANT: load/store对于第一个成员使用最大对齐

        auto containsStructType = [](Type *type, StructType *targetStruct) {
            while (auto *arrayType = dyn_cast<ArrayType>(type))
                type = arrayType->getElementType();
            return type == targetStruct;
        };

        // Replace global/static initializers
        for (auto &global : module.globals()) {
            if (!global.hasInitializer() || !containsStructType(global.getValueType(), sty)) continue;
            bool isDirectStructGlobal = global.getValueType() == sty;
            if (isDirectStructGlobal) {
                global.setAlignment(Align(maxAlign));
                // 更新全局变量的初始化器
                auto *initializer = global.getInitializer();
                auto *newInitializer = changeGVInitializer(initializer, sty);
                if (newInitializer && newInitializer != initializer) {
                    global.setInitializer(newInitializer);
                }
            }
            
            // 结构体全局变量的user替换
            SmallVector<User*, 4> users;
            for (auto *user : global.users())
                users.push_back(user);
            for (auto *user : users) {
                if (isa<Instruction>(user)) {
                    bool directFirstMemberAccess = false;
                    if (auto *load = dyn_cast<LoadInst>(user))
                        directFirstMemberAccess = load->getPointerOperand() == &global;
                    else if (auto *store = dyn_cast<StoreInst>(user))
                        directFirstMemberAccess = store->getPointerOperand() == &global;

                    // 直接load/store结构体地址时，LLVM IR省略了成员GEP，实际读写的是第一个成员。
                    // 对结构体数组全局变量也会出现这种形式，例如NPB_FT中dcomplex u[NX]的u[0].real。
                    if (directFirstMemberAccess)
                        repairUserType(&global, user, memberNewBaseTypes[0], maxAlign);
                    else
                        repairUserType(&global, user, global.getValueType(), maxAlign);
                } else if (auto *constant = dyn_cast<ConstantExpr>(user)) {  // 读写的是后续成员
                    auto *gepOp = dyn_cast<GEPOperator>(constant);
                    if (!gepOp || gepOp->getSourceElementType() != sty || gepOp->getNumIndices() != 2) {
                        repairUserType(&global, constant, global.getValueType(), maxAlign);
                        continue;
                    }
                    auto *memberIndexValue = dyn_cast<ConstantInt>((gepOp->idx_end() - 1)->get());
                    if (!memberIndexValue) {
                        repairUserType(&global, constant, global.getValueType(), maxAlign);
                        continue;
                    }
                    unsigned memberIndex = memberIndexValue->getZExtValue();
                    if (memberIndex >= memberNewBaseTypes.size()) {
                        repairUserType(&global, constant, global.getValueType(), maxAlign);
                        continue;
                    }
                    // errs() << "[repair struct member users] " << global.getName() << '.' << memberIndex << '\n';
                    SmallVector<User*, 4> constantUsers;
                    for (auto *user : constant->users())
                        constantUsers.push_back(user);
                    for (auto *user : constantUsers)
                        repairUserType(constant, user, memberNewBaseTypes[memberIndex], maxAlignTillNow[memberIndex]);
                } else {
                    errs() << "[TODO] unhandled struct's user: " << *user << '\n';
                }
            }
        }

        // 结构体局部变量的user替换
        for (auto &F : module) {
            for (auto &BB : F) {
                for (auto &I : BB) {
                    if (auto *gep = dyn_cast<GetElementPtrInst>(&I)) {
                        if (gep->getSourceElementType() != sty) continue;  // 此处逻辑与repairGEPInst不同
                        // 更新GEP类型
                        SmallVector<Value*, 4> indices;
                        for (auto &idx : gep->indices()) {
                            indices.push_back(idx.get());
                        }
                        auto *newGEP = GetElementPtrInst::CreateInBounds(
                            sty,
                            gep->getPointerOperand(),
                            indices,
                            gep->getName(),
                            gep->getIterator()
                        );
                        gep->replaceAllUsesWith(newGEP);
                        instToErase.insert(gep);

                        if (auto *originalAlloca = findOriginalAlloca(gep->getPointerOperand())) 
                            originalAlloca->setAlignment(Align(maxAlign));

                        SmallVector<User*, 4> users;
                        for (auto *user : newGEP->users())
                            users.push_back(user);

                        // Only two-index GEPs select a field from this struct type here.
                        // GEPs like "gep %struct, ptr %base, i64 %idx" index a struct array element;
                        // keep the whole struct type for the following member GEP users.
                        if (gep->getNumIndices() == 2) {
                            if (auto *memberIndexValue = dyn_cast<ConstantInt>((gep->idx_end() - 1)->get())) {
                                unsigned memberIndex = memberIndexValue->getZExtValue();
                                if (memberIndex < memberNewBaseTypes.size()) {
                                    for (auto *user : users)
                                        repairUserType(newGEP, user, memberNewBaseTypes[memberIndex], maxAlignTillNow[memberIndex]);
                                    continue;
                                }
                            }
                        }

                        for (auto *user : users)
                            repairUserType(newGEP, user, sty, maxAlign);
                    }
                }
            }
        }
    }
}

void ChangeVariableDecl::changeGlobalDeclarations(Module &module) {
    json::Value* globalVal = config.getAsObject()->get("global");
    if (!globalVal || globalVal->kind() == json::Value::Null) return;
    auto &context = module.getContext();
    
    // 获取全局变量配置
    auto* globalArr = globalVal->getAsArray();
    if (!globalArr) return;
    
    for (auto &globalEntry : *globalArr) {
        auto* entryObj = globalEntry.getAsObject();
        if (!entryObj) continue;
        auto nameOpt = entryObj->getString("name");
        std::string varName = nameOpt ? nameOpt->str() : "";
        configMap.insert_or_assign(varName, globalEntry);
    }

    for (auto &GV : module.globals()) {
        if (GV.isDeclaration()) continue;
        auto name = demangle(GV.getName().str());
        // Skip vars not declared by user
        if(name.find('.') != string::npos) continue;

        auto it = configMap.find(name);
        if (it == configMap.end()) continue;
        auto &configEntry = it->second;
        auto* entryObj = configEntry.getAsObject();
        auto typeOpt = entryObj->getString("type");
        std::string typeName = typeOpt ? typeOpt->str() : "";
        auto *targetType = typeNameToBasicType(typeName, context);
        changeGlobalVariable(&GV, targetType);
    }
}

Type* ChangeVariableDecl::getBasicType(Value *value) {
    // 沿着user chain找三类基类型：local variable / global variable / function argument
    // 递归查找来自形参的类型
    std::function<Type*(Value*)> findOriginalArgType = [this](Value* v) -> Type* {
        while (true) {
            // errs() << "[debug findOriginalArgType]" << *v << '\n';
            if (auto *load = dyn_cast<LoadInst>(v)) {
                auto *from = load->getPointerOperand();
                for (auto *user : from->users()) {
                    if (user == from) continue;
                    if (auto *store = dyn_cast<StoreInst>(user)) {
                        if (auto *arg = dyn_cast<Argument>(store->getOperand(0))) {
                            auto *argType = arg->getType();
                            if (argType->isPointerTy()) {
                                return argPtrBaseTypeMap[arg];
                            }
                            return argType;
                        }
                    }
                }
                return nullptr;
            }
            else if (auto *gep = dyn_cast<GetElementPtrInst>(v))
                v = gep->getPointerOperand();
            else
                break;
        }
        return nullptr;
    };
    if (auto *argType = findOriginalArgType(value)) return argType;

    auto stripArrayType = [](Type *type) -> Type* {
        while (auto *arrayType = dyn_cast<ArrayType>(type))
            type = arrayType->getElementType();
        return type;
    };
    // 尝试找到原始的AllocaInst
    if (auto *alloca = findOriginalAlloca(value)) {
        return localBaseTypeMap[alloca];
    } else if (auto *globalVar = dyn_cast<GlobalVariable>(value)) {
        return globalBaseTypeMap[globalVar];
    } else if (auto *gep = dyn_cast<GetElementPtrInst>(value)) {
        return stripArrayType(gep->getResultElementType());
    } else if (auto *gep = dyn_cast<GEPOperator>(value)) {
        return stripArrayType(gep->getResultElementType());
    }
    // TODO: 是否有其他情形？
    errs() <<"[Unhandled basic type]" << *value << '\n';
    return nullptr;
}

// 创建具有新参数与返回值类型的函数副本
Function* ChangeVariableDecl::cloneFunction(Function *oldFunc, std::map<int, Type*> &argBaseTypes) {
    if (!oldFunc || oldFunc->isDeclaration()) return nullptr;

    // get cloned function's unique key
    std::ostringstream oss;
    // callee的函数名的第一个小数点前的子串部分，从而保证是原名
    oss << oldFunc->getName().str().substr(0, oldFunc->getName().str().find('.'));
    if (funcRetTypeMap.count(oldFunc)) oss << ":" << (void*)funcRetTypeMap[oldFunc];
    for (auto &p : argBaseTypes) oss << "," << p.first << ":" << (void*)p.second;
    std::string key = oss.str();
    auto it = cloneCache.find(key);
    if (it != cloneCache.end()) return it->second;

    FunctionType *oldFuncType = oldFunc->getFunctionType();
    SmallVector<Type*, 4> newArgTypes;
    int argCnt = oldFuncType->getNumParams();
    for (int i = 0; i < argCnt; ++i) {
        if (argBaseTypes.count(i) && !oldFuncType->getParamType(i)->isPointerTy()) {
            newArgTypes.push_back(argBaseTypes[i]);
        } else {
            newArgTypes.push_back(oldFuncType->getParamType(i));
        }
    }

    // 确定返回类型，当前仅支持变换浮点类型返回值
    Type *returnType = oldFuncType->getReturnType();
    if (funcRetTypeMap.find(oldFunc) != funcRetTypeMap.end() && returnType->isFloatingPointTy()) {
        returnType = funcRetTypeMap[oldFunc];
    }
    
    FunctionType *newFuncType = FunctionType::get(
        returnType,
        newArgTypes,
        oldFuncType->isVarArg()
    );

    Function *newFunc = Function::Create(
        newFuncType,
        oldFunc->getLinkage(),
        oldFunc->getName(),
        oldFunc->getParent()
    );

    if (!newFunc) {
        errs() << "[Error] Failed to create new function\n";
        return nullptr;
    }

    DenseMap<CallInst*, std::pair<unsigned, unsigned>> oldCallInstsInfo;
    for (auto &BB : *oldFunc) {
        for (auto &I : BB) {
            if (auto *oldCall = dyn_cast<CallInst>(&I)) {
                oldCallInstsInfo[oldCall] = {oldCall->getDebugLoc().getLine(), oldCall->getDebugLoc().getCol()};
            }
        }
    }

    // 使用 CloneFunction 函数来克隆函数体
    // 首先在 VMap 中添加旧函数参数到新函数参数的映射
    ValueToValueMapTy VMap;
    auto oldArgIt = oldFunc->arg_begin();
    auto newArgIt = newFunc->arg_begin();
    for (int i = 0; i < argCnt && oldArgIt != oldFunc->arg_end(); ++i, ++oldArgIt, ++newArgIt) {
        VMap[&(*oldArgIt)] = &(*newArgIt);
    }

    SmallVector<ReturnInst*, 8> Returns;  
    CloneFunctionInto(newFunc, oldFunc, VMap, CloneFunctionChangeType::LocalChangesOnly, Returns);
    cloneCache[key] = newFunc;

    // Copy local base-type mappings from original allocas to cloned allocas
    for (auto &BB : *oldFunc) {
        for (auto &I : BB) {
            if (auto *oldAlloca = dyn_cast<AllocaInst>(&I)) {
                auto it = VMap.find(oldAlloca);
                if (it != VMap.end()) {
                    if (auto *newAlloca = dyn_cast<AllocaInst>(it->second)) {
                        auto lbIt = localBaseTypeMap.find(oldAlloca);
                        if (lbIt != localBaseTypeMap.end()) {
                            localBaseTypeMap[newAlloca] = lbIt->second;
                        }
                    }
                }
            }
        }
    }

    // 替换newFunc函数体中形参相关的user指令，使之匹配
    // errs() << "[repair cloned function arg users]" << newFunc->getName() << '\n';
    for (auto &BB : *newFunc) {
        for (auto &I : BB) {
            if(auto *Store = dyn_cast<StoreInst>(&I)) {
                if (auto* arg = dyn_cast<Argument>(Store->getOperand(0))) {
                    // 接下来沿着arg的user链变换精度为argBaseType，对于指针类型形参，则需要一直load/GEP抵达最基精度才能变换为argBaseType
                    unsigned argIndex = arg->getArgNo();
                    if (argBaseTypes.count(argIndex)) {
                        auto *targetType = argBaseTypes[argIndex];
                        if (targetType->isPointerTy()) continue;
                        argPtrBaseTypeMap[arg] = targetType;
                        errs() << "[clone function's arg store type] "<< *Store << ':' << *targetType << '\n';
                        auto *storeTo = Store->getOperand(1);
                        changeAllocaInst(findOriginalAlloca(storeTo), targetType);

                        // Store已自动变换为targetType，将变换传入到所有user
                        if (auto *dstOp = dyn_cast<Instruction>(storeTo))
                            for (auto *user : dstOp->users())
                                repairUserType(dstOp, user, targetType);
                    }
                }
            } else if (auto *ret = dyn_cast<ReturnInst>(&I)) {
                repairReturnInst(ret);
            }
        }
    }

    // 上述修复完成后，clonedFunc内部的callInst的callee实参-形参类型可能不匹配，进一步修复
    for (auto &BB : *newFunc) {
        for (auto &I : BB) {
            if (auto * call = dyn_cast<CallInst>(&I)) {
                // TODO: 从所有【创建】的函数副本中，检查CalledFunction的实参-形参类型是否匹配
                if (auto *callee = call->getCalledFunction()) {
                    // 遍历被调函数的实参类型，检查一致性
                    std::map<int, Type*> innerArgBaseTypes;
                    unsigned innerNumArgs = call->arg_size();
                    for (unsigned ai = 0; ai < innerNumArgs; ++ai) {
                        Value *arg = call->getArgOperand(ai);
                        Type *baseType = nullptr;
                        auto *argType = arg->getType();
                        if (argType->isPointerTy()) {
                            baseType = getBasicType(arg);
                        }
                        else if (argType->isFloatingPointTy())
                            // if caller provides an FP and callee has declaration override, consider it
                            if (funcDeclTypeMap.count(callee) && funcDeclTypeMap[callee].count(ai))
                                if (argType != funcDeclTypeMap[callee][ai])
                                    baseType = funcDeclTypeMap[callee][ai];

                        if (baseType && baseType->isFloatingPointTy())
                            innerArgBaseTypes[ai] = baseType;
                    }

                    bool innerNeedsClone = !innerArgBaseTypes.empty() ||
                                           (funcRetTypeMap.count(callee) &&
                                            funcRetTypeMap[callee] != callee->getReturnType());
                    if (innerNeedsClone)
                        if (auto *innerNewFunc = cloneFunction(callee, innerArgBaseTypes))
                            updateCallInst(call, *innerNewFunc, innerArgBaseTypes);
                }
            }
        }
    }

    return newFunc;
}

void ChangeVariableDecl::updateCallInst(CallInst *call, Function &newFunc, std::map<int, Type*> &declTypeMap) {
    SmallVector<Value*, 16> newArgs;
    // 处理参数类型转换
    for (unsigned i = 0; i < call->arg_size(); ++i) {
        Value *oldArg = call->getArgOperand(i);
        // 检查是否为Cast指令，获取原始类型和操作数
        if (auto *castInst = dyn_cast<CastInst>(oldArg)) {
            oldArg = castInst->getOperand(0);
            // 将原始cast指令添加到待删除列表，消除冗余
            instToErase.insert(castInst);
        }
        Type *targetType = nullptr;
        // 检查该参数是否需要类型转换
        if (declTypeMap.count(i)) {
            targetType = declTypeMap[i];
        } else {
            // 如果没有明确指定，则使用新函数的参数类型
            if (i < newFunc.getFunctionType()->getNumParams()) {
                targetType = newFunc.getFunctionType()->getParamType(i);
            } else {
                // 变长参数保持原样
                targetType = oldArg->getType();
            }
        }
        
        // 如果类型不匹配，添加类型转换
        if (oldArg->getType() != targetType && !oldArg->getType()->isPointerTy()) {
            if (Value *castedArg = insertCast(oldArg, targetType, call)) {
                newArgs.push_back(castedArg);
            } else {
                // 转换失败，使用原参数
                errs() << "[ERROR updateCallInst()] fail to cast old arg " << *oldArg << '\n';
                newArgs.push_back(oldArg);
            }
        } else {
            newArgs.push_back(oldArg);
        }
    }
    
    // 创建新的CallInst
    CallInst *newCall = CallInst::Create(newFunc.getFunctionType(), &newFunc, newArgs, "", call->getIterator());
    newCall->setDebugLoc(call->getDebugLoc());
    newCall->takeName(call);
    replaceMultiOperandInst(call, newCall);

    SmallVector<User*, 8> users;
    for (auto *user : newCall->users())
        users.push_back(user);
    for (auto *user : users)
        repairUserType(newCall, user);
    // errs() << "[update caller] " << *call << " -> " << *newCall << "\n";
}

// Replace multi-operand Inst safely (such as BinOp, FCmpInst, CallInst)
void ChangeVariableDecl::replaceMultiOperandInst(Instruction* oldInst, Instruction* newInst) {
    if (instReplaceMap.count(oldInst)) { // has other replacement, replace uses & remove before
        auto *beforeInst = instReplaceMap[oldInst];
        beforeInst->replaceAllUsesWith(newInst);
        instToErase.insert(beforeInst);
    } else {  // no replacement, just replace uses
        oldInst->replaceAllUsesWith(newInst);
        instToErase.insert(oldInst);
    }
    instReplaceMap[oldInst] = newInst;
}

// Remove insts without users, they are from MultiOperandInsts (BinOp, FCmpInst)
void ChangeVariableDecl::removeUnusedReplacements() {
    for (auto &pair : instReplaceMap) {
        auto *candidate = pair.second;
        if (candidate->use_empty()) {
            // errs() << "[remove unused inst in multi-op] " << *candidate << '\n';
            instToErase.insert(candidate);
        }
    }
    instReplaceMap.clear();
}

DIType* ChangeVariableDecl::createDIType(Type* type, DIScope* scope, DIBuilder* DIB) {
    // 处理数组类型
    if (auto *arrayType = dyn_cast<ArrayType>(type)) {
        Type *elemType = arrayType->getElementType();
        DIType *elemDIType = createDIType(elemType, scope, DIB);
        if (!elemDIType) return nullptr;

        uint64_t elemSize = elemType->getPrimitiveSizeInBits();
        uint64_t arraySize = arrayType->getNumElements();
        uint64_t totalSize = elemSize * arraySize;
        uint64_t align = elemType->getPrimitiveSizeInBits();  // 按位对齐

        // 创建子范围数组（将单个DISubrange包装为DINodeArray）
        auto *subrange = DIB->getOrCreateSubrange(0, arraySize);
        auto subranges = DIB->getOrCreateArray({subrange});
        
        return DIB->createArrayType(totalSize, align, elemDIType, subranges);
    }

    // 处理基础浮点类型
    if (type->isFloatingPointTy()) {
        StringRef name;
        unsigned size;
        if (type->isHalfTy()) {
            name = "__fp16";
            size = 16;
        } else if (type->isFloatTy()) {
            name = "float";
            size = 32;
        } else if (type->isDoubleTy()) {
            name = "double";
            size = 64;
        } else if (type->isFP128Ty()) {
            name = "long double";
            size = 128;
        } else {
            errs() << "Unsupported float type for debug info: " << *type << "\n";
            return nullptr;
        }
        return DIB->createBasicType(name, size, dwarf::DW_ATE_float);
    }

    errs() << "Unsupported type for debug info: " << *type << "\n";
    return nullptr;
}

void ChangeVariableDecl::updateCallGraph(CallGraphNode* node) {
    DenseSet<Function*> visited;
    std::function<void(CallGraphNode*)> update = [&](CallGraphNode* currNode) {
        if (!currNode || visited.count(currNode->func)) return;
        visited.insert(currNode->func);

        SmallVector<std::pair<CallInst*, CallGraphNode*>, 16> children;
        for (auto &entry : currNode->children)
            children.push_back({entry.first, entry.second});

        for (auto &entry : children) {
            auto *callInst = entry.first;
            auto *childNode = entry.second;
            // 如果我只更新root，后面的让它沿着updateCallInst自动更新？是否就不需要CallGraph了？

            Function *origCallee = childNode ? childNode->func : nullptr;
            if (!callInst || !origCallee) continue;

            // compute arg base types for this call
            std::map<int, Type*> argBaseTypes;
            unsigned numArgs = callInst->arg_size();
            for (unsigned i = 0; i < numArgs; ++i) {
                Value *arg = callInst->getArgOperand(i);
                Type *baseType = nullptr;
                auto *argType = arg->getType();
                if (argType->isPointerTy())
                    baseType = getBasicType(arg); // pointer base follows actual arg
                else if (argType->isFloatingPointTy())
                    // consult declared desired param type if any (guarded access)
                    if (funcDeclTypeMap.count(origCallee) && funcDeclTypeMap[origCallee].count(i))
                        if (argType != funcDeclTypeMap[origCallee][i])
                            baseType = funcDeclTypeMap[origCallee][i];

                if (baseType && baseType->isFloatingPointTy())
                    argBaseTypes[i] = baseType;
            }

            // decide whether cloning is required
            bool needsClone = !argBaseTypes.empty() ||
                            (funcRetTypeMap.count(origCallee) &&
                            funcRetTypeMap[origCallee] != origCallee->getReturnType());
            if (needsClone)
                if (auto *targetCallee = cloneFunction(origCallee, argBaseTypes))
                    updateCallInst(callInst, *targetCallee, argBaseTypes);
        }

        for (auto &entry : children)
            update(entry.second);
    };

    update(node);
}

CallGraphNode* ChangeVariableDecl::buildCallGraph(Function* currFunc) {
    auto it = funcToNode.find(currFunc);
    if (it != funcToNode.end())
        return it->second;

    auto* currNode = new CallGraphNode(currFunc);
    funcToNode[currFunc] = currNode;
    for (auto &BB : *currFunc) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *callee = call->getCalledFunction();
                if (!callee || callee->isDeclaration()) continue;
                currNode->children[call] = buildCallGraph(callee);
            }
        }
    }
    return currNode;
}

void ChangeVariableDecl::dumpCallGraph(CallGraphNode* node, int layer, DenseSet<Function*> &visited) {
    if (!node) return;
    for (int i = 0; i < layer; ++i) errs() << '\t';
    if (visited.find(node->func) != visited.end()) {
        errs() << node->func->getName() << " already dumped\n";
        return;
    }
    visited.insert(node->func);
    errs() << node->func->getName() << '\n';
    for (auto &child : node->children)
        dumpCallGraph(child.second, layer + 1, visited);
}

bool ChangeVariableDecl::runOnModule(Module &module) {
    DL = &module.getDataLayout();
    parseConfig();

    // 记录所有可变参数调用的原始参数类型
    for(auto &f : module) {
        for(auto &bb : f) {
            for(auto &i : bb) {
                if(auto *call = dyn_cast<CallInst>(&i)) {
                    auto *function = call->getCalledFunction();
                    if (!function) continue;
                    unsigned numFixedArgs = function->getFunctionType()->getNumParams();
                    // 跳过固定参数调用
                    if(numFixedArgs == call->arg_size()) continue;
                    // 否则，记录所有参数类型
                    for (unsigned i = 0; i < call->arg_size(); ++i) {
                        varargCallOriginalTypes[call].push_back(call->getArgOperand(i)->getType());
                        // errs() << "[record vararg call's original type] " << i << ' ' << *call << '\n';
                    }
                }
            }
        }
    }

    initBaseTypeMaps(module);
    DIB = new DIBuilder(module);
    changeStructDeclarations(module);
    changeGlobalDeclarations(module);
    for(auto &f : module) {
        string name = f.getName().str();
        if(!f.isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            runOnFunction(f);
        }
    }
    // 清理global/static的旧GV
    for (auto *GV : GVToErase)
        GV->eraseFromParent();
    GVToErase.clear();

    auto *CallGraph = buildCallGraph(module.getFunction("main"));

    // ONLY FOR DEBUG
    errs() << ">>> Initial Call Graph <<<\n";
    DenseSet<Function*> visited;
    dumpCallGraph(CallGraph, 0, visited);

    // 检查实参、形参不一致情形，创建副本并替换call指令
    updateCallGraph(CallGraph);

    // ONLY FOR DEBUG
    errs() << ">>> Updated Call Graph <<<\n";
    for (auto &p : funcToNode) delete p.second;
    funcToNode.clear();
    CallGraph = buildCallGraph(module.getFunction("main"));
    visited.clear();
    dumpCallGraph(CallGraph, 0, visited);

    for (auto &F : module) repairCasts(F);
    for (auto *inst : instToErase) inst->eraseFromParent();
    instToErase.clear();
    
    if (verifyModule(module, &errs()))
    {
        errs() << "Module verify failed, IR as below:\n";
        module.print(errs(), nullptr);
        errs() << "<<< IR OVER <<<\n";
        exit(1);
    } else {
        errs() << "Module verify success!\n";
    }

    DIB->finalize();
    delete DIB;
    return true;
}  

void ChangeVariableDecl::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

PreservedAnalyses ChangeVariableDecl::run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << ">>> Perci-Tuner: Running ChangeVariableDecl Pass...\n";    
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
