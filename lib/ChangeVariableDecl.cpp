// @Author: Guocheng & Guosiyuan NUDT
// Usage: Change variables' precision according to json.
// Input: Variable's type config
// Output: Precision-Changed IR (unrunnable because of incorrect casts)

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
    alloca->setAllocatedType(newType);
    alloca->setAlignment(getAlign(newType));
    newAllocas.insert(alloca);

    errs() << *alloca << '\n';
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
    Value *retVal = ret->getReturnValue();
    if (!retVal) return nullptr;  // void return
    
    Type *funcReturnType = ret->getFunction()->getReturnType();
    if (retVal->getType() != funcReturnType) {
        if (auto *castValue = insertCast(retVal, funcReturnType, ret)) {
            ret->setOperand(0, castValue);
            return dyn_cast<Instruction>(ret);
        }
    }
    return nullptr;  // nothing changed
}

Instruction* ChangeVariableDecl::repairLoadInst(LoadInst* load, Type* targetType) {
    if (load->getType()->isIntegerTy()) return nullptr; // ignore integer load
    auto *ptrOperand = load->getPointerOperand();
    Type* loadType = targetType;
    if (auto *allocaInst = findOriginalAlloca(ptrOperand))
        if (!loadType)
            loadType = pointeeTypeMap[allocaInst];
    if (loadType) {
        if (auto *arrayType = dyn_cast<ArrayType>(loadType))
            loadType = arrayType->getElementType();
        if (loadType != load->getType()) {
            auto *newLoad = new LoadInst(loadType, ptrOperand, load->getName(), load->isVolatile(), load);
            newLoad->setAlignment(getAlign(loadType));
            load->replaceAllUsesWith(newLoad);
            instToErase.insert(load);
            errs() << "[repair load] " << *load << "->" << *newLoad << '\n';
            return newLoad;
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairStoreInst(StoreInst* store, Type* targetType) {
    Value *oldValue = store->getOperand(0);
    auto *ptrOperand = store->getPointerOperand();
    Type* storeType = targetType;
    if (auto *allocaInst = findOriginalAlloca(ptrOperand))
        if (!storeType)
            storeType = allocaInst->getAllocatedType();
    if (storeType) {
        if (auto *arrayType = dyn_cast<ArrayType>(storeType))
            storeType = arrayType->getElementType();
        if (oldValue->getType() != storeType) {
            errs() << "[repair store] " << *store << "->";
            if (auto *castValue = insertCast(oldValue, storeType, store)) {
                store->setOperand(0, castValue);
                store->setAlignment(getAlign(storeType));
            }
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
        Type* currentType = call->getType();
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
        bool needsRepair = highestType != currentType;
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
void ChangeVariableDecl::repairUserType(User *user, Type* targetType) {
    if (!isa<Instruction>(user)) {
        errs() << "[skipping unuser]\n";
        return;
    }

    auto *inst = cast<Instruction>(user);
    Instruction *newInst = nullptr;
    errs() << "[checking user] " << *inst << "\n";

    if (auto ret = dyn_cast<ReturnInst>(inst))
        newInst = repairReturnInst(ret);
    else if (auto load = dyn_cast<LoadInst>(inst))
        newInst = repairLoadInst(load, targetType);
    else if (auto store = dyn_cast<StoreInst>(inst))
        newInst = repairStoreInst(store, targetType);
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
        for (auto *user : newInst->users()) {
            repairUserType(user, targetType);
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
Type* ChangeVariableDecl::translateValidTypeName(string typeName, LLVMContext& context)
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
    }
    else 
    if (typeName.find("float") != string::npos) {
        targetType = Type::getFloatTy(context);
    } else if (typeName.find("long double") != string::npos) {
        targetType = Type::getFP128Ty(context);
    } else if (typeName.find("double") != string::npos) {
        targetType = Type::getDoubleTy(context);
    } else {
        errs() << "Unknown type: " << typeName << ", ignored.\n";
        return nullptr;
    }
    
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
            if (targetTypeName == srcTypeName) continue;
            auto *targetType = translateValidTypeName(targetTypeName, context);
            if (targetType == nullptr) continue;
            
            errs() << "[change return type] " << F.getName() << ": " << srcTypeName << "->" << *targetType << "\n";
            funcDeclTypeMap[&F][-1] = targetType;
            break;
        }
    }

    // Check if decl inst need to be changed
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
                Value *Addr = SI->getPointerOperand();
                if (auto *GV = dyn_cast<GlobalVariable>(Addr)) {
                    // Check local static vars
                    if (GV->hasInternalLinkage()) {
                        auto DL = I.getDebugLoc();
                        if (DL) {
                            auto *localScope = llvm::dyn_cast<llvm::DILocalScope>(DL.getScope());
                            string fileName = localScope->getNonLexicalBlockFileScope()->getFile()->getFilename().str();

                            std::string key = fileName + ":" + std::to_string(DL.getLine()) + ":" + std::to_string(DL.getCol());
                            auto it = configMap.find(key);
                            if (it == configMap.end()) continue;

                            auto &configEntry = it->second;
                            auto typeName = configEntry["type"].get<string>();
                            auto srcTypeName = parseTypeName(GV->getValueType());
                            if (typeName == srcTypeName) continue;
                            auto *targetType = translateValidTypeName(typeName, context);
                            if (targetType == nullptr) continue;

                            // 变换本地静态变量声明，形如@expm.tp = internal global [25 x fp128] zeroinitializer, align 16 -> @expm.tp = internal global [25 x double] zeroinitializer, align 8
                            errs() << "[change global] " << *GV << " -> ";
                            
                            // 准备新的初始化器
                            Constant *newInitializer = nullptr;
                            if (GV->hasInitializer()) {
                                Constant *initializer = GV->getInitializer();
                                if (isa<ConstantAggregateZero>(initializer)) {
                                    // 如果原初始化器是zeroinitializer，创建新类型的zeroinitializer
                                    newInitializer = ConstantAggregateZero::get(targetType);
                                }
                                // TODO: 处理其他类型的初始化器
                            }
                            
                            // 创建新的全局变量
                            GlobalVariable *newGV = new GlobalVariable(
                                *GV->getParent(),
                                targetType,
                                GV->isConstant(),
                                GV->getLinkage(),
                                newInitializer,
                                GV->getName()
                            );
                            
                            // 复制原全局变量的属性
                            newGV->setAlignment(getAlign(targetType));
                            newGV->copyAttributesFrom(GV);
                            newGV->setThreadLocalMode(GV->getThreadLocalMode());
                            newGV->setExternallyInitialized(GV->isExternallyInitialized());
                            
                            GV->replaceAllUsesWith(newGV);
                            errs() << *newGV << "\n";
                            
                            // 修复所有使用该全局变量的users指令
                            for (User *user : newGV->users()) {
                                repairUserType(user, targetType);
                            }
                            GV->eraseFromParent();
                        }
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
                    auto typeName = configEntry["type"].get<string>();
                    auto srcTypeName = parseDITypeName(DV->getType());
                    if (typeName == srcTypeName) continue;
                    auto *targetType = translateValidTypeName(typeName, context);
                    if (targetType == nullptr) continue;
                    
                    if (typeName.find("*") != string::npos) {  // ptr's AllocaInst don't need change
                        auto *alloca = dyn_cast<AllocaInst>(DDI->getAddress());
                        pointeeTypeMap[alloca] = targetType;  // save pointee's basic type
                        newAllocas.insert(alloca);
                    } else {
                        if (configEntry["isParam"]) {
                            unsigned argIndex = 0;
                            for (auto argIt = F.arg_begin(); argIt != F.arg_end(); ++argIt, ++argIndex) {
                                if (argIt->getName() == varName) {
                                    errs() << "[change arg] " << argIt->getName() << ": " << srcTypeName << "->" << *targetType << '\n';
                                    funcDeclTypeMap[&F][argIndex] = targetType;
                                    break;
                                }
                            }
                        }
                        changeAllocaInst(dyn_cast<AllocaInst>(DDI->getAddress()), targetType);
                        
                        // 更新调试信息
                        auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable());
                        if (!DV) {
                            errs() << "Warning: No debug info for variable, skipping debug update\n";
                            break;
                        }
                        // 创建新的DIType（调试类型）
                        DIScope *scope = DV->getScope();
                        DIType *newDIType = createDIType(targetType, scope, DIB);
                        if (!newDIType) {
                            errs() << "Warning: Failed to create new debug type, skipping debug update\n";
                            break;
                        }
                        // 创建新的DILocalVariable（调试变量描述）
                        auto *newDV = DIB->createAutoVariable(
                            scope,               // 保持原作用域
                            DV->getName(),       // 添加：变量名称
                            DV->getFile(),       // 保持原文件
                            DV->getLine(),       // 保持原行号
                            newDIType,           // 更新为新类型
                            DV->isArtificial(),  // 修改：isAlwaysPreserved -> isArtificial
                            DV->getFlags()       // 保留flags，移除后续不存在的参数
                        );
                        DDI->setVariable(newDV);
                    }
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
                    auto *targetType = translateValidTypeName(typeName, context);
                    if (targetType == nullptr) continue;

                    if (typeName.find("*") != string::npos) {  // ptr's AllocaInst don't need change
                        pointeeTypeMap[alloca] = targetType;  // save pointee's basic type
                        newAllocas.insert(alloca);
                    } else {
                        if (configEntry["isParam"]) {
                            unsigned argIndex = 0;
                            for (auto argIt = F.arg_begin(); argIt != F.arg_end(); ++argIt, ++argIndex) {
                                if (argIt->getName() == varName) {
                                    errs() << "[change arg] " << argIt->getName() << ": " << srcTypeName << "->" << *targetType << '\n';
                                    funcDeclTypeMap[&F][argIndex] = targetType;
                                    break;
                                }
                            }
                        }
                        changeAllocaInst(alloca, targetType);
                    }
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
    if (!config.contains("structs")) return;

    nlohmann::json structInfos = config["structs"];
    auto &context = module.getContext();
    // Iterate all struct decls in the module
    for (auto *sty : module.getIdentifiedStructTypes()) {
        StringRef rawName = sty->getName();
        StringRef structName = rawName.starts_with("struct.") ? rawName.substr(7) : rawName;
        if (structInfos.contains(structName.str())) {
            auto members = structInfos[structName.str()];
            SmallVector<Type*, 4> newElements;
            bool needsUpdate = false;

            // Iterate original struct members
            for (unsigned i = 0; i < sty->getNumElements(); ++i) {
                auto *newType = sty->getElementType(i);  // now is old type
            
                string typeStr = members[i]["type"].get<string>();
                auto *targetType = translateValidTypeName(typeStr, context);

                if (targetType && targetType != newType) {
                    errs() << "[change struct member] " << structName << " member " << i << ": " << *newType << "->" << *targetType << '\n';
                    newType = targetType;
                    needsUpdate = true;
                }
            
                newElements.push_back(newType);
            }

            if (needsUpdate) {
                sty->setBody(newElements, sty->isPacked());
                
                // Replace global/static initializers
                for (auto &global : module.globals()) {
                    if (!global.hasInitializer()) continue;
                    if (auto init = dyn_cast<ConstantStruct>(global.getInitializer())) {
                        if (init->getType() == sty) {
                            SmallVector<Constant*, 4> newElements;
                            for (unsigned i = 0; i < init->getNumOperands(); i++) {
                                auto *targetType = sty->getElementType(i);
                                auto *elem = init->getOperand(i);
                                
                                // Floating-type conversion
                                if (auto *fp = dyn_cast<ConstantFP>(elem)) {
                                    auto val = fp->getValueAPF();
                                    bool losesInfo;
                                    val.convert(sty->getElementType(i)->getFltSemantics(),
                                               APFloat::rmNearestTiesToEven,
                                               &losesInfo);  // Add lost info parameter
                                    elem = ConstantFP::get(targetType, val);
                                }
                                newElements.push_back(elem);
                            }
                            global.setInitializer(ConstantStruct::get(sty, newElements));

                            // Recalculate alignment
                            const auto &DL = module.getDataLayout();
                            unsigned align = DL.getABITypeAlign(sty).value();
                            global.setAlignment(MaybeAlign(align));
                        }
                    }
                }

                // Get max align
                unsigned maxAlign = 0;
                const auto &DL = module.getDataLayout();
                for (unsigned i = 0; i < sty->getNumElements(); ++i) {
                    auto *elemType = sty->getElementType(i);
                    unsigned align = DL.getABITypeAlign(elemType).value();
                    if (align > maxAlign) {
                        maxAlign = align;
                    }
                }

                // Repair GEP
                for (auto &F : module) {
                    for (auto &BB : F) {
                        for (auto &I : BB) {
                            if (auto *gep = dyn_cast<GetElementPtrInst>(&I)) {
                                if (gep->getSourceElementType() == sty) {
                                    IRBuilder<> builder(gep);
                                    SmallVector<Value*, 4> indices;
                                    for (auto &idx : gep->indices()) {
                                        indices.push_back(idx.get());
                                    }
                                    auto *newGEP = builder.CreateInBoundsGEP(
                                        sty,
                                        gep->getPointerOperand(),
                                        indices
                                    );
                                    gep->replaceAllUsesWith(newGEP);
                                    instToErase.insert(gep);

                                    if (auto *alloca = dyn_cast<AllocaInst>(gep->getPointerOperand())) {
                                        alloca->setAlignment(Align(maxAlign));
                                    }

                                    for (auto *user : newGEP->users()) {
                                        if (auto *store = dyn_cast<StoreInst>(user)) {
                                            auto *value = store->getValueOperand();
                                            auto *ptr = store->getPointerOperand();
                                            if (ptr == newGEP) {
                                                auto *structType = cast<StructType>(gep->getSourceElementType());
                                                auto *lastIndex = gep->idx_end() - 1; // 获取最后一个索引
                                                unsigned elementIndex = cast<ConstantInt>(lastIndex->get())->getZExtValue();
                                                auto *elementType = structType->getElementType(elementIndex);

                                                if (value->getType() != elementType) {
                                                    IRBuilder<> builder(store);
                                                    auto *newValue = builder.CreateFPCast(value, elementType);
                                                    store->setOperand(0, newValue);
                                                    store->setAlignment(Align(maxAlign));
                                                    for (auto *user : store->users())
                                                        repairUserType(user);
                                                }
                                            }
                                        }
                                        else if (auto *load = dyn_cast<LoadInst>(user)) {
                                            auto *lastIndex = gep->idx_end() - 1; // 获取最后一个索引
                                            unsigned elementIndex = cast<ConstantInt>(lastIndex->get())->getZExtValue();
                                            auto *elemType = sty->getElementType(elementIndex);
                                            
                                            auto *newLoad = new LoadInst(
                                                elemType,
                                                newGEP,
                                                "",  // empty name
                                                load->isVolatile(),
                                                load
                                            );
                                            newLoad->setAlignment(Align(maxAlign));
                                            
                                            load->replaceAllUsesWith(newLoad);
                                            instToErase.insert(load);
                                            for (auto *loadUser : newLoad->users())
                                                repairUserType(loadUser);
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
}

Function* ChangeVariableDecl::changeFunctionDecl(Function &F, DenseMap<int, Type*> declTypeMap) {
    if (declTypeMap.empty()) return &F;
    auto retNewType = funcDeclTypeMap[&F].lookup(-1);

    FunctionType *oldFuncType = F.getFunctionType();
    SmallVector<Type*, 8> newArgTypes;
    for (unsigned i = 0; i < oldFuncType->getNumParams(); ++i) {
        if (declTypeMap.count(i)) {
            newArgTypes.push_back(declTypeMap.lookup(i));
        } else {
            newArgTypes.push_back(oldFuncType->getParamType(i));
        }
    }

    FunctionType *newFuncType = FunctionType::get(
        retNewType ? retNewType : oldFuncType->getReturnType(),
        newArgTypes,
        oldFuncType->isVarArg()
    );

    // 创建一个新的函数，其类型为新的函数类型
    Function *newFunc = Function::Create(
        newFuncType,
        F.getLinkage(),
        F.getName(),
        F.getParent()
    );

    // 复制原函数的属性到新函数
    newFunc->copyAttributesFrom(&F);

    // 把原函数的基本块转移到新函数
    while (!F.empty()) {
        BasicBlock *BB = &F.front();
        BB->removeFromParent();
        BB->insertInto(newFunc);
    }

    // 用新函数的参数替换原函数参数的所有使用
    for (auto &oldArg : F.args()) {
        auto &newArg = *(newFunc->arg_begin() + oldArg.getArgNo());
        oldArg.replaceAllUsesWith(&newArg);
    }

    // 如果返回值类型发生变化，修改函数内部的ret指令，TODO: 如果IR自动创建了retval的alloca，此处不会处理retval类型
    if (retNewType && retNewType != oldFuncType->getReturnType()) {
        IRBuilder<> builder(newFunc->getContext());
        for (auto &BB : *newFunc) {
            Instruction *term = BB.getTerminator();
            if (auto *retInst = dyn_cast<ReturnInst>(term)) {
                // 如果ret指令有操作数（不是void返回），进行类型转换
                if (retInst->getNumOperands() > 0) {
                    Value *oldRetVal = retInst->getOperand(0);
                    // 检查是否需要类型转换
                    if (oldRetVal->getType() != retNewType) {
                        builder.SetInsertPoint(retInst);
                        Value *newRetVal = insertCast(oldRetVal, retNewType, retInst);
                        if (newRetVal) {
                            retInst->setOperand(0, newRetVal);
                        } else {
                            errs() << "[change func decl] Failed to cast return value to type " << *retNewType << "\n";
                        }
                    }
                } else {
                    // 如果原函数返回void，但新函数不是void，这是一个错误
                    if (!retNewType->isVoidTy()) {
                        errs() << "[change func decl] Error: Function was void returning but new type is " << *retNewType << "\n";
                    }
                }
            }
        }
    }
    errs() << "[change func decl] " << *oldFuncType << "->" << *newFuncType << "\n";

    return newFunc;
}

void ChangeVariableDecl::updateCaller(Function &oldFunc, Function &newFunc, DenseMap<int, Type*>& declTypeMap) {
    // 首先收集所有需要处理的CallInst，避免遍历过程中修改集合
    SmallVector<CallInst*, 16> callInsts;
    for (User *U : oldFunc.users()) {
        if (CallInst *call = dyn_cast<CallInst>(U)) {
            callInsts.push_back(call);
        }
    }
    
    // 处理每个CallInst
    for (CallInst *call : callInsts) {
        // 直接创建新的CallInst，调用newFunc
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
            if (oldArg->getType() != targetType) {
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
    
    funcToErase.insert(&oldFunc);
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
                    unsigned numFixedArgs = call->getCalledFunction()->getFunctionType()->getNumParams();
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
    for(auto &f : module) {
        string name = f.getName().str();
        if(!f.isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            runOnFunction(f);
        }
    }

    // 遍历整个funcDeclTypeMap，将其中的函数替换
    DenseMap<Function*, Function*> replaceMap;
    for (auto &funcEntry : funcDeclTypeMap) {
        auto *functionPtr = funcEntry.first;
        auto newFunc = changeFunctionDecl(*functionPtr, funcEntry.second);
        replaceMap[functionPtr] = newFunc;
    }

    for (auto &funcEntry : replaceMap) {
        auto *oldFunc = funcEntry.first;
        auto *newFunc = funcEntry.second;
        updateCaller(*oldFunc, *newFunc, funcDeclTypeMap[oldFunc]);
    }
    for (auto *F : funcToErase) {
        errs() << "[erase old function] " << F->getName() << '\n';
        F->eraseFromParent();
    }
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