// @Author: Guocheng & Guosiyuan NUDT
// Usage: Change variables' precision according to json.
// Input: Variable's type config
// Output: Precision-Changed IR

#include "ChangeVariableDecl.h"
#include "utils/ParseTypeName.hpp"
#include "nlohmann/json.hpp"

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
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Config/llvm-config.h>

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<string> VariableConfigName("variable-config", cl::value_desc("filename"), cl::desc("Variable's Type Config"), cl::init("config.json"));

void ChangeVariableDecl::parseConfig() {
    std::ifstream configFile(VariableConfigName.c_str());
    if (!configFile.is_open()) {
        errs() << "Error: Failed to open " << VariableConfigName << "\n";
        exit(1);
    }

    configFile >> config;
    nlohmann::json &localConfig = config["local"];
    
    for (auto &funcEntry : localConfig.items())
        includedFunctions.insert(funcEntry.key());
    for (auto &funcEntry : localConfig) {
        // 处理每个函数的变量配置
        for (auto &varEntry : funcEntry) {
            std::string file = varEntry["file"];
            int line = varEntry["line"];
            int column = varEntry["column"];
            if (varEntry["name"] == "ret" && !line && !column) continue;
            
            std::string key = file + ":" + std::to_string(line) + ":" + std::to_string(column);
            configMap[key] = varEntry;
        }
    }
}

Align ChangeVariableDecl::getAlign(Type* targetType) {
    if (targetType->isPointerTy()) {
        return DL->getPointerPrefAlignment();
    } else if (auto *arrayType = dyn_cast<ArrayType>(targetType)) {
        return getAlign(arrayType->getElementType());
    } else {
        return Align(targetType->getPrimitiveSizeInBits() / 8);
    }
}

AllocaInst* ChangeVariableDecl::changeAllocaInst(AllocaInst *alloca, Type *newType) {
    errs() << "[change alloca] " << *alloca << "->";
    // 判断是否为指针类型
    if (alloca->getAllocatedType()->isPointerTy()) {
        // 对于指针类型，保存指向类型的基本类型
        pointeeTypeMap[alloca] = newType;
        newAllocas.insert(alloca);
    } else {
        // 对于非指针类型，更改AllocaInst的类型
        alloca->setAllocatedType(newType);
        alloca->setAlignment(getAlign(newType));
        newAllocas.insert(alloca);
    }
    errs() << *alloca << '\n';
    
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
                        errs() << "[update alloca's DItype] " << DV->getName() << " type: " << *newType << typeSuffix << '\n';
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
            auto *oldType = load->getType();

            if (oldType->isPointerTy()) {  // keep pointee type
                Type* ptrBaseType = nullptr;
                if (auto* alloca = findOriginalAlloca(load->getPointerOperand())) {
                    ptrBaseType = pointeeTypeMap[alloca];
                }
                for (auto *user : load->users())
                    repairUserType(user, ptrBaseType);
                continue;
            }

            if (oldType == newType) continue;

            auto *newLoad = new LoadInst(newType, load->getPointerOperand(), "", load);
            newLoad->setAlignment(align);

            load->replaceAllUsesWith(newLoad);
            instToErase.insert(load);    // erase later to avoid broken iterator
            for (auto *user : newLoad->users())
                repairUserType(user);
        }
        else repairUserType(user, newType);
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
            return ConstantExpr::getFPCast(constantVal, targetType);
        }
        return nullptr;
    }
#endif

    IRBuilder<> builder(insertBefore);

    if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        errs() << "[insert cast] " << *val->getType() << "->" << *targetType << "\n";
        return builder.CreateFPCast(val, targetType, "fp_cast");
    }
    if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy()) {
        errs() << "[insert cast] " << *val->getType() << "->" << *targetType << "\n";
        return builder.CreateSIToFP(val, targetType, "int_to_fp");
    }
    if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy()) {
        errs() << "[insert cast] " << *val->getType() << "->" << *targetType << "\n";
        return builder.CreateFPToSI(val, targetType, "fp_to_int");
    }
    
    errs() << "[unsupported cast] " << *val->getType() << "->" << *targetType << "\n";
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
    errs() << "[repair return]" << *ret << " -> ";
    Value *retVal = ret->getReturnValue();
    if (!retVal) return nullptr;  // void return
    
    Type *funcReturnType = ret->getFunction()->getReturnType();
    if (retVal->getType() != funcReturnType) {
        if (auto *castValue = insertCast(retVal, funcReturnType, ret)) {
            ret->setOperand(0, castValue);
            return dyn_cast<Instruction>(ret);
        }
    }
    errs() << *ret << '\n';
    return nullptr;  // nothing changed
}

Instruction* ChangeVariableDecl::repairLoadInst(LoadInst* load, Type* targetType, unsigned align) {
    if (load->getType()->isIntegerTy()) return nullptr; // ignore integer load
    if (load->getType()->isPointerTy()) return load;
    auto *ptrOperand = load->getPointerOperand();
    Type* loadType = targetType;
    if (auto *allocaInst = findOriginalAlloca(ptrOperand))
        if (!loadType)
            loadType = pointeeTypeMap[allocaInst];
    if (loadType) {
        if (auto *arrayType = dyn_cast<ArrayType>(loadType))
            loadType = arrayType->getElementType();
        if (load->getType()->isPointerTy()) {
            load->setAlignment(align ? Align(align) : getAlign(loadType));
            return load;
        }
        if (loadType != load->getType()) {
            auto *newLoad = new LoadInst(loadType, ptrOperand, load->getName(), load->isVolatile(), load);
            newLoad->setAlignment(align ? Align(align) : getAlign(loadType));
            newLoad->setDebugLoc(load->getDebugLoc());
            load->replaceAllUsesWith(newLoad);
            instToErase.insert(load);
            errs() << "[repair load] " << *load << "->" << *newLoad << '\n';
            return newLoad;
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairStoreInst(StoreInst* store, Type* targetType, unsigned align) {
    if (store->getType()->isIntegerTy()) return nullptr; // ignore integer store
    if (store->getOperand(1)->getType()->isPointerTy()) return store;
    Value *oldValue = store->getOperand(0);
    auto *ptrOperand = store->getPointerOperand();
    Type* storeType = targetType;
    if (auto *allocaInst = findOriginalAlloca(ptrOperand))
        if (!storeType)
            storeType = allocaInst->getAllocatedType();
    if (storeType) {
        if (auto *arrayType = dyn_cast<ArrayType>(storeType))
            storeType = arrayType->getElementType();
        store->setAlignment(align ? Align(align) : getAlign(storeType));
        if (oldValue->getType() != storeType) {
            errs() << "[repair store] " << *store << "->";
            if (auto *castValue = insertCast(oldValue, storeType, store))
                store->setOperand(0, castValue);
            errs() << *store << '\n';
        }
    }
    return store;
}

Instruction* ChangeVariableDecl::repairUnaryOp(UnaryOperator* unaOp) {
    auto *operand = unaOp->getOperand(0);
    if (operand->getType() != unaOp->getType()) {
        errs() << "[repair unary op] " << *unaOp << "->";
        IRBuilder<> builder(unaOp);
        auto *newUnaOp = builder.CreateUnOp(unaOp->getOpcode(), operand);
        newUnaOp->takeName(unaOp);
        if (auto *newUnaOpInst = dyn_cast<Instruction>(newUnaOp)) {
            newUnaOpInst->setDebugLoc(unaOp->getDebugLoc());
        }
        unaOp->replaceAllUsesWith(newUnaOp);
        instToErase.insert(unaOp);
        errs() << *newUnaOp << '\n';
        return dyn_cast<Instruction>(newUnaOp);
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairBinaryOp(BinaryOperator* binOp) {
    SmallVector<Value*, 2> newOperands;
    auto *highestType = findHighestPrecisionType(binOp);

    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            // 将原始cast指令添加到待删除列表，消除冗余
            instToErase.insert(castInst);
        }
        // type of src operands not match
        if (operand->getType() != highestType) {
            if (auto *newOperand = insertCast(operand, highestType, binOp)) {
                operand = newOperand;
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
        errs() << "[repair binop] " << *binOp << "->";
        binOp = dyn_cast<BinaryOperator>(newBinOp);
    } else {
        for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
            binOp->setOperand(i, newOperands[i]);
        }
    }
    errs() << *binOp << '\n';
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
        errs() << "[repair FCmp] " << *fcmp << "->";
        fcmp = dyn_cast<FCmpInst>(newFcmp);
    } else {
        for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
            fcmp->setOperand(i, newOperands[i]);
        }
    }
    errs() << *fcmp << '\n';
    return fcmp;
}

Instruction* ChangeVariableDecl::repairCallInst(CallInst* call) {
    auto *callee = call->getCalledFunction();
    if (!callee) return nullptr;
    
    // 处理llvm.fmuladd函数调用
    if (callee->getName().startswith("llvm.fmuladd")) {
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

        // 更新newOperands并插入必要cast
        for (unsigned i = 0; i < 3; ++i) {
            Value* source = newOperands[i];
            if (source->getType() != highestType) {
                if (auto* casted = insertCast(source, highestType, call)) {
                    newOperands[i] = casted;
                }
            }
        }

        // 检查操作数是否需要转换（无论当前类型是否与最高精度类型相同）
        bool needsRepair = highestType != oldType;
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
            CallInst* newCall = CallInst::Create(newFunc, castedOperands, "", call);
            newCall->setDebugLoc(call->getDebugLoc());
            newCall->takeName(call);
            replaceMultiOperandInst(call, newCall);
            errs() << "[repair fmuladd] " << *call << "->";
            call = newCall;
        } else {
            // 直接替换操作数类型即可
            for (unsigned i = 0; i < 3; ++i) {
                call->setArgOperand(i, newOperands[i]);
            }
        }
        errs() << *call << '\n';
        return call;
    }
    
    // 处理其他函数调用
    bool modified = false;
    unsigned numFixedArgs = callee->getFunctionType()->getNumParams();
    errs() << "[repair call] " << *call << "->";
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
                errs() << "[repair call] Failed to cast arg" << i << " to type " << *targetType << '\n';
            }
        }
    }

    if (modified) {
        errs() << *call << '\n';
        return call;
    }
    errs() << '\n';
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
        auto *newPhi = PHINode::Create(highestType, phi->getNumIncomingValues(), "", phi);
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
        errs() << "[repair phi] " << *phi << "->" << *newPhi << '\n';
        return dyn_cast<Instruction>(newPhi);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairGEPInst(GetElementPtrInst* gep, Type* targetType) {
    // 多级指针的情形，保持targetType状态，返回原指令，直到访问到最低一级的元素类型，将其转换为targetType
    if (gep->getSourceElementType()->isPointerTy()) return gep;
    if (!targetType || gep->getSourceElementType() == targetType) {
        return nullptr; // 类型一致，无需替换
    }

    // 提取 GEP 指令的索引
    SmallVector<Value*, 4> indices;
    for (auto& index : gep->indices()) {
        indices.push_back(index.get());
    }

    // 创建新的 GEP 指令
    auto* newGEP = GetElementPtrInst::CreateInBounds(
        targetType,
        gep->getPointerOperand(),
        indices,
        gep->getName(),
        gep
    );

    // 用新的 GEP 指令替换旧的 GEP 指令
    gep->replaceAllUsesWith(newGEP);
    instToErase.insert(gep);
    errs() << "[repair GEP] " << *gep << "->" << *newGEP << '\n';

    return newGEP;
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
        errs() << "Failed to cast source value in BitCastInst: " << *bitCast << "\n";
        return nullptr;
    }

    BitCastInst* newBitCast = new BitCastInst(newSrcValue, destType, bitCast->getName(), bitCast);
    bitCast->replaceAllUsesWith(newBitCast);
    instToErase.insert(bitCast);
    errs() << "[repair bitcast] " << *bitCast << "->" << *newBitCast << '\n';

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
        errs() << "[repair IntToFP] " << *castInst << "->";
        
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
        errs() << *newCast << '\n';
        return newCastInst;
    }
    
    return nullptr;
}

// Make inst operands type consistent
void ChangeVariableDecl::repairUserType(User *user, Type* targetType, unsigned align) {
    if (!isa<Instruction>(user)) {
        errs() << "[not an instruction]\n";
        return;
    }

    auto *inst = cast<Instruction>(user);
    Instruction *newInst = nullptr;
    errs() << "[checking user] " << *inst << "\n";

    if (auto ret = dyn_cast<ReturnInst>(inst))
        newInst = repairReturnInst(ret);
    else if (auto load = dyn_cast<LoadInst>(inst))
        newInst = repairLoadInst(load, targetType, align);
    else if (auto store = dyn_cast<StoreInst>(inst))
        newInst = repairStoreInst(store, targetType, align);
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
        // 防止修改迭代器导致无限递归
        SmallVector<User*> users;
        for (auto *user : newInst->users())
            users.push_back(user);
        for (auto *user : users)
            repairUserType(user, targetType);
        users.clear();
    }
}

// Change global & static & local static to target type
void ChangeVariableDecl::changeGlobalVariable(GlobalVariable *GV, Type* targetType) {
    if (!targetType || targetType == GV->getValueType() || !GV->hasInitializer()) return;
    auto *initializer = GV->getInitializer();
    auto *newInitializer = GV->getValueType()->isPointerTy() ? 
                            initializer : changeGVInitializer(initializer, targetType);
    if (!newInitializer) return;
    GlobalVariable *newGV = GV;
    if (newInitializer != initializer) {
        errs() << "[change GV] " << *GV << " -> ";
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
        errs() << *newGV << '\n';
    }

    repairGVUsers(newGV, targetType);  // TODO: use newGV cause inifinite user loop
    if (initializer != newInitializer) {
        GVToErase.insert(GV);
    }
}

Constant* ChangeVariableDecl::changeGVInitializer(Constant *initializer, Type* targetType) {
    if (isa<ConstantAggregateZero>(initializer)) {
        // 如果原初始化器是zeroinitializer，创建新类型的zeroinitializer
        return ConstantAggregateZero::get(targetType);
    } else if (isa<ConstantFP>(initializer)) {
        // TODO: 该语句并不生效，为什么？
        // newInitializer = ConstantFP::get(targetType, cfp->getValueAPF());
        return ConstantExpr::getFPCast(initializer, targetType);
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
    errs() << "Ignored intializer: " << *initializer << '\n';
    return nullptr;
}

void ChangeVariableDecl::repairGVUsers(GlobalVariable *GV, Type* targetType) {
    auto *GVType = GV->getValueType();
    for (auto *user : GV->users()) {
        if (isa<Instruction>(user)) {  // 数组索引为0
            repairUserType(user, targetType);
        } else if (auto *constant = dyn_cast<ConstantExpr>(user)) {
            // 处理GEP常量表达式用户（数组索引>1）
            errs() << "[repair GEP constant expr] " << *constant << "->";
            
            // 获取原GEP的参数
            SmallVector<Value*, 8> indices;
            for (unsigned i = 1; i < constant->getNumOperands(); ++i) {
                indices.push_back(constant->getOperand(i));
            }

            // 检查是否是inbounds GEP
            bool isInBounds = constant->getRawSubclassOptionalData();
            // 创建新的GEP表达式
            auto *newConstant = ConstantExpr::getGetElementPtr(
                GVType->isPointerTy() ? GVType : targetType,
                GV,
                indices,
                isInBounds
            );
            errs() << *newConstant << '\n';
            // 替换原常量表达式的所有使用
            constant->replaceAllUsesWith(newConstant);
            
            for (auto *constantUser : newConstant->users()) {
                repairUserType(constantUser, targetType);
            }
        }
    }
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
                    errs() << "[remove unused cast] " << *castInst << " in " << F.getName() << '\n';
                    instToErase.insert(castInst);
                    continue;
                }
                
                // 2. 处理相同类型的冗余cast
                if (castInst->getSrcTy() == castInst->getDestTy()) {
                    errs() << "[remove redundant cast] " << *castInst << '\n';
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
                            
                            errs() << "[replace cast chain] " << *castInst << "->" << *newCast << '\n';
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
Type* ChangeVariableDecl::typeNameToType(string typeName, LLVMContext& context)
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
        errs() << "Unknown type: " << typeName << ", ignored.\n";
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
    auto functionConfig = config["local"][demangle(F.getName())];

    // 检查是否需要修改函数返回值类型
    for (auto &configEntry : functionConfig) {
        if (configEntry["name"] == "ret.") {
            auto srcTypeName = parseTypeName(F.getFunctionType()->getReturnType());
            auto targetTypeName = configEntry["type"].get<string>();
            // TODO: 暂不处理指针类型返回值
            if (targetTypeName.find('*') != string::npos) continue;
            auto *targetType = typeNameToType(targetTypeName, context);
            if (targetType == nullptr) continue;
            
            errs() << "[change return type] " << F.getName() << ": " << srcTypeName << "->" << *targetType << "\n";
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
                        auto typeName = configEntry["type"].get<string>();
                        auto *targetType = typeNameToType(typeName, context);
                        changeGlobalVariable(GV, targetType);
                    }
                }
            }
            // 适用clang情况
            else if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                    auto fileName = DV->getFile()->getFilename().str();
                    auto loc = DDI->getDebugLoc();

                    std::string key = fileName + ":" + std::to_string(loc.getLine()) + ":" + std::to_string(loc.getCol());
                    auto it = configMap.find(key);
                    if (it == configMap.end()) continue;
                    auto &configEntry = it->second;
                    auto typeName = configEntry["type"].get<string>();
                    auto srcTypeName = parseDITypeName(DV->getType());
                    if (typeName == srcTypeName) continue;
                    auto *targetType = typeNameToType(typeName, context);
                    if (targetType == nullptr) continue;
                    
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
                    auto typeName = configEntry["type"].get<string>();
                    if (typeName == srcTypeName) continue;
                    auto *targetType = typeNameToType(typeName, context);
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

void ChangeVariableDecl::updateCalleeToCallInsts(Function *F) {
    for (auto &BB : *F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                // 如果是用户定义的函数，则将浮点或者指针的实参类型记录到multiVersionCallMap
                auto *func = dyn_cast<Function>(call->getCalledFunction());
                // 被调函数必须在IR中已经定义了函数体，否则直接跳过
                if (func->isDeclaration()) continue;
                std::map<int, Type*> argBaseTypes;
                for (unsigned i = 0; i < call->arg_size(); ++i) {
                    Value *arg = call->getArgOperand(i);
                    auto *argBaseType = arg->getType();
                    Type *baseType = nullptr;
                    
                    // 从DebugInfo中获取原始类型，特别是指针类型的基类型
                    if (argBaseType->isPointerTy()) {
                        // 尝试从DebugInfo中获取指针的基类型
                        baseType = getBasicTypeFromDebugInfo(arg);
                    }
                    else if (argBaseType->isFloatingPointTy()) {
                        // 对于浮点类型实参，如果是cast指令，需要找到最原始的源操作数和类型
                        Value *originalArg = arg;
                        baseType = argBaseType;
                        // 沿着cast链向上查找，直到找到最原始的值
                        while (auto *castInst = dyn_cast<CastInst>(originalArg)) {
                            baseType = castInst->getSrcTy();
                            if (!baseType->isFloatingPointTy()) break;
                            originalArg = castInst->getOperand(0);
                        }
                    }
                    if (baseType && baseType->isFloatingPointTy())
                        argBaseTypes[i] = baseType;
                }
                if (argBaseTypes.size() || (funcRetTypeMap.find(func) != funcRetTypeMap.end() && funcRetTypeMap[func] != func->getReturnType())) {
                    calleeToCallInsts[{func, argBaseTypes}].push_back(call);
                }
            }
        }
    }
}

void ChangeVariableDecl::changeStructDeclarations(Module &module) {
    if (!config.contains("structs")) return;

    nlohmann::json structInfos = config["structs"];
    auto &context = module.getContext();
    // Iterate all struct decls in the module
    for (auto *sty : module.getIdentifiedStructTypes()) {
        StringRef rawName = sty->getName();
        StringRef structName = rawName.starts_with("struct.") ? rawName.substr(7) : rawName;
        if (!structInfos.contains(structName.str())) continue;
        auto members = structInfos[structName.str()];
        SmallVector<Type*, 4> memberNewTypes, memberNewBaseTypes;  // 成员的完整类型和基类型
        bool needsUpdate = false;

        // Iterate original struct members
        for (unsigned i = 0; i < sty->getNumElements(); ++i) {
            auto *oldType = sty->getElementType(i);
        
            string typeStr = members[i]["type"].get<string>();
            auto *targetType = typeNameToType(typeStr, context);

            if (targetType && targetType != oldType) {
                if (oldType->isPointerTy())
                    oldType = PointerType::get(targetType, 0);
                else {
                    errs() << "[change struct member] " << structName << " member " << i << ": " << *oldType << "->" << *targetType << '\n';
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

        // Replace global/static initializers
        for (auto &global : module.globals()) {
            if (!global.hasInitializer() || global.getValueType() != sty) continue;
            global.setAlignment(Align(maxAlign));
            
            // 更新全局变量的初始化器
            auto *initializer = global.getInitializer();
            auto *newInitializer = changeGVInitializer(initializer, sty);
            if (newInitializer && newInitializer != initializer) {
                global.setInitializer(newInitializer);
            }
            
            // 结构体全局变量的user替换
            for (auto *user : global.users()) {
                // 若为storeInst/loadInst，说明读写的是第一个成员，将memberNewTypes[0]的类型作为替换目标
                if (isa<Instruction>(user)) {
                    repairUserType(user, memberNewBaseTypes[0], maxAlign);
                } else if (auto *constant = dyn_cast<ConstantExpr>(user)) {  // 读写的是后续成员
                    unsigned memberIndex = cast<ConstantInt>(constant->getOperand(constant->getNumOperands() - 1))->getZExtValue();
                    errs() << "[repair struct member users] " << global.getName() << '.' << memberIndex << '\n';
                    for (auto *user : constant->users()) {
                        repairUserType(user, memberNewBaseTypes[memberIndex], maxAlignTillNow[memberIndex]);
                    }
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
                            gep
                        );
                        gep->replaceAllUsesWith(newGEP);
                        instToErase.insert(gep);

                        if (auto *originalAlloca = findOriginalAlloca(gep->getPointerOperand())) 
                            originalAlloca->setAlignment(Align(maxAlign));

                        // 获取成员在结构体中的索引及其类型
                        unsigned elementIndex = cast<ConstantInt>(gep->idx_end() - 1)->getZExtValue();
                        for (auto *user : newGEP->users())
                            repairUserType(user, memberNewBaseTypes[elementIndex], maxAlignTillNow[elementIndex]);
                    }
                }
            }
        }
    }
}

void ChangeVariableDecl::changeGlobalDeclarations(Module &module) {
    if (!config.contains("global")) return;
    auto &context = module.getContext();
    
    // 获取全局变量配置
    nlohmann::json &globalConfig = config["global"];
    
    for (auto &globalEntry : globalConfig) {
        std::string varName = globalEntry["name"];
        configMap[varName] = globalEntry;
    }

    for (auto &GV : module.globals()) {
        if (GV.isDeclaration()) continue;
        auto name = demangle(GV.getName().str());
        // Skip vars not declared by user
        if(name.find('.') != string::npos) continue;

        auto it = configMap.find(name);
        if (it == configMap.end()) continue;
        auto &configEntry = it->second;
        std::string typeName = configEntry["type"].get<string>();
        auto *targetType = typeNameToType(typeName, context);
        changeGlobalVariable(&GV, targetType);
    }
}

// 从DebugInfo中获取指针的基类型，递归处理多级指针
Type* ChangeVariableDecl::getBasicTypeFromDebugInfo(Value *value) {
    // 首先尝试找到原始的AllocaInst
    AllocaInst *alloca = findOriginalAlloca(value);
    if (alloca) {
        errs() << "[getBasicTypeFromDebugInfo alloca] " << *alloca << '\n';
        
        // 遍历函数中的DbgDeclareInst，查找与当前AllocaInst相关的调试信息
        Function *func = alloca->getFunction();
        if (!func) return nullptr;
        
        errs() << "[getBasicTypeFromDebugInfo function] " << func->getName() << '\n';
        for (auto &BB : *func) {
            for (auto &I : BB) {
                if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                    if (DDI->getAddress() == alloca) {
                        if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                            DIType *diType = DV->getType();
                            errs() << "[getBasicTypeFromDebugInfo DIType] " << *diType << '\n';
                            // 递归获取最基础的类型
                            while (diType) {
                                if (auto *derivedType = dyn_cast<DIDerivedType>(diType)) {
                                    if (derivedType->getTag() == dwarf::DW_TAG_pointer_type || 
                                        derivedType->getTag() == dwarf::DW_TAG_reference_type) {
                                        diType = derivedType->getBaseType();
                                    } else break;
                                } else break;
                            }
                            // 将DIType转换为LLVM Type
                            if (diType) {
                                LLVMContext &context = alloca->getContext();
                                if (auto *basicType = dyn_cast<DIBasicType>(diType)) {
                                    StringRef name = basicType->getName();
                                    errs() << "[getBasicTypeFromDebugInfo basicType] " << name << '\n';
                                    if (name == "float") {
                                        return Type::getFloatTy(context);
                                    } else if (name == "double") {
                                        return Type::getDoubleTy(context);
                                    } else if (name == "half") {
                                        return Type::getHalfTy(context);
                                    } else if (name == "long double") {
                                        return Type::getFP128Ty(context);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (auto *globalVar = dyn_cast<GlobalVariable>(value)) {
        Type *globalType = globalVar->getInitializer()->getType();
        errs() << "[getBasicTypeFromDebugInfo global initializer] " << *globalType << '\n';
        
        if (auto *arrayType = dyn_cast<ArrayType>(globalType)) {
            Type *elementType = arrayType->getArrayElementType();
            errs() << "[getBasicTypeFromDebugInfo global initializer base type] " << *elementType << '\n';
            return elementType;
        } else {
            return globalType;
        }
    }
    return nullptr;
}

// 创建具有新参数类型的函数副本
Function* ChangeVariableDecl::cloneFunctionWithNewArgs(Function *oldFunc, std::map<int, Type*> &argBaseTypes) {
    if (!oldFunc) return nullptr;

    FunctionType *oldFuncType = oldFunc->getFunctionType();
    SmallVector<Type*, 4> newArgTypes;
    for (unsigned i = 0; i < oldFuncType->getNumParams(); ++i) {
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

    // 使用 CloneFunction 函数来克隆函数体
    // 首先在 VMap 中添加旧函数参数到新函数参数的映射
    ValueToValueMapTy VMap;
    auto oldArgIt = oldFunc->arg_begin();
    auto newArgIt = newFunc->arg_begin();
    for (unsigned i = 0; i < oldFuncType->getNumParams() && oldArgIt != oldFunc->arg_end(); ++i, ++oldArgIt, ++newArgIt) {
        VMap[&(*oldArgIt)] = &(*newArgIt);
    }
    Function *clonedFunc = CloneFunction(oldFunc, VMap);

    // 将克隆的函数体移动到新函数
    while (!clonedFunc->empty()) {
        BasicBlock *BB = &clonedFunc->front();
        BB->removeFromParent();
        BB->insertInto(newFunc);
    }

    // 删除克隆的函数，避免生成额外的 declare 语句
    clonedFunc->eraseFromParent();
    newFunc->copyAttributesFrom(oldFunc);

    // 替换newFunc函数体中形参相关的user指令，使之匹配
    for (auto &BB : *newFunc) {
        for (auto &I : BB) {
            if(auto *Store = dyn_cast<StoreInst>(&I))
            {
                if (auto* arg = dyn_cast<Argument>(Store->getOperand(0))) {
                    // 接下来沿着arg的user链变换精度为argBaseType，对于指针类型形参，则需要一直load/GEP抵达最基精度才能变换为argBaseType
                    unsigned argIndex = arg->getArgNo();
                    if (argBaseTypes.count(argIndex)) {
                        Type *targetType = argBaseTypes[argIndex];
                        errs() << "[Clone functions's Store type] "<< *Store << ':' << *targetType << '\n';
                        auto *storeTo = Store->getOperand(1);
                        changeAllocaInst(findOriginalAlloca(storeTo), targetType);

                        // Store已自动变换为targetType，将变换传入到所有user
                        for (auto *user : storeTo->users())
                            repairUserType(user, targetType);
                    }
                }
            }
            else if (auto *ret = dyn_cast<ReturnInst>(&I)) {
                repairReturnInst(ret);
            }
        }
    }
    // 克隆的函数包含的CallInst，同样需支持多版本
    updateCalleeToCallInsts(newFunc);

    return newFunc;
}

void ChangeVariableDecl::updateCaller(CallInst *call, Function &newFunc, std::map<int, Type*> &declTypeMap) {
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
                newArgs.push_back(oldArg);
            }
        } else {
            newArgs.push_back(oldArg);
        }
    }
    
    // 创建新的CallInst
    CallInst *newCall = CallInst::Create(newFunc.getFunctionType(), &newFunc, newArgs, "", call);
    newCall->setDebugLoc(call->getDebugLoc());
    newCall->takeName(call);
    replaceMultiOperandInst(call, newCall);
    for (auto *user : newCall->users())
        repairUserType(user);
    errs() << "[update caller] " << *call << " -> " << *newCall << "\n";
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
            errs() << "[remove unused inst in multi-op] " << *candidate << '\n';
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
                        errs() << "[record vararg call's original type] " << i << ' ' << *call << '\n';
                    }
                }
            }
        }
    }

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

    // 根据calleeToCallInsts来创建函数副本
    for (auto &F : module)
        updateCalleeToCallInsts(&F);
    for (auto entry : calleeToCallInsts) {
        auto functionArgs = entry.first;
        auto callInsts = entry.second;
        // 对于每组不同的functionArgs，创建一个函数副本
        auto *func = functionArgs.first;
        auto &argBaseTypes = functionArgs.second;
        auto *newFunc = cloneFunctionWithNewArgs(func, argBaseTypes);
        for (auto *call : callInsts)
            functionReplaceMap[call] = newFunc;

        // 输出每个函数副本的func和对应callInsts
        errs() << "-----------------------------\n";
        errs() << "[cloned Function] " << newFunc->getName() << " arg base types: \n";
        for (auto &pair : argBaseTypes)
            errs() << pair.first << ' ' << *(pair.second) << '\n';
        errs() << "[" << newFunc->getName() << "'s callInsts]\n";
        for (auto *call : callInsts)
            errs() << *call << '\n';
        errs() << "-----------------------------\n";
    }
    // 待所有函数副本创建完成后，再做替换函数调用
    for (auto entry : calleeToCallInsts) {
        auto functionArgs = entry.first;
        auto callInsts = entry.second;
        auto &argBaseTypes = functionArgs.second;
        // 将替换后的函数副本作为callInsts的新callee
        for (auto *call : callInsts) {
            // 替换前的函数，【可能】需要移除
            funcToErase.insert(call->getCalledFunction());
            if(functionReplaceMap[call])
                updateCaller(call, *functionReplaceMap[call], argBaseTypes);
        }
    }

    for (auto &F : module) repairCasts(F);
    for (auto *inst : instToErase) inst->eraseFromParent();
    instToErase.clear();

    for (auto *oldFunc : funcToErase) {
        errs() << "[remove unused function] " << oldFunc->getName() << '\n';
        if(oldFunc->use_empty()) oldFunc->eraseFromParent();
    }
    funcToErase.clear();
    
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