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

    std::string configContent((std::istreambuf_iterator<char>(configFile)), std::istreambuf_iterator<char>());
    configFile.close();
    
    config = nlohmann::json::parse(configContent);
    for (auto &entry : config["local"].items()) {
        includedFunctions.insert(entry.key());
    }
}

AllocaInst* ChangeVariableDecl::changeAllocaInst(DbgDeclareInst *DDI, Type *newType) {
    auto *var = DDI->getAddress();
    Align align;

    if (auto *arrayType = dyn_cast<ArrayType>(newType)) {
        auto *elementType = arrayType->getElementType();
        align = Align(elementType->getPrimitiveSizeInBits() / 8);
    } else {
        align = Align(newType->getPrimitiveSizeInBits() / 8);
    }

    auto alloca = dyn_cast<AllocaInst>(var);
    IRBuilder<> builder(alloca);
    auto *newAlloca = builder.CreateAlloca(newType, nullptr, alloca->getName());
    newAlloca->setAlignment(align);
    newAlloca->takeName(alloca);
    newAllocas.insert(newAlloca);
    instToErase.insert(alloca);
    alloca->replaceAllUsesWith(newAlloca);

    errs() << "[change alloca] " << *alloca << "->" << *newAlloca << '\n';
    // 更新调试信息
    auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable());
    if (!DV) {
        errs() << "Warning: No debug info for variable, skipping debug update\n";
        return newAlloca;
    }

    // 创建新的DIType（调试类型）
    DIScope *scope = DV->getScope();
    DIType *newDIType = createDIType(newType, scope, DIB);
    if (!newDIType) {
        errs() << "Warning: Failed to create new debug type, skipping debug update\n";
        return newAlloca;
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

    return newAlloca;
}

void ChangeVariableDecl::repairAllocaUsers(AllocaInst* alloca) {
    auto newType = alloca->getAllocatedType();
    Align align;

    if (auto *arrayType = dyn_cast<ArrayType>(newType)) {
        auto *elementType = arrayType->getElementType();
        align = Align(elementType->getPrimitiveSizeInBits() / 8);
    } else if (newType->isPointerTy()) {
        align = Align(alloca->getModule()->getDataLayout().getPointerPrefAlignment());
    } else {
        align = Align(newType->getPrimitiveSizeInBits() / 8);
    }

    for (auto *user : alloca->users()) {
        if (auto store = dyn_cast<StoreInst>(user)) {
            // StoreInst has no users, replace its operand directly.
            auto *oldValue = store->getOperand(0);
            if (auto *newValue = insertCast(oldValue, newType, store)) {
                store->setOperand(0, newValue);
                store->setAlignment(align);
            }
        }
        else if (auto load = dyn_cast<LoadInst>(user)) {
            auto *oldType = load->getType();

            if (oldType->isPointerTy()) {  // don't replace ptr's LoadInst, directly replace user
                for (auto *user : load->users())
                    repairUserType(user);
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
        else if (auto gep = dyn_cast<GetElementPtrInst>(user)) {
            SmallVector<Value*, 4> indices;
            for (auto &index : gep->indices()) {
                indices.push_back(index.get());
            }

            auto *newGEP = GetElementPtrInst::Create(
                newType,
                gep->getPointerOperand(),
                indices,
                gep->getName(),         // inst name
                gep                     // inst loc
            );

            gep->replaceAllUsesWith(newGEP);
            instToErase.insert(gep);
            for (auto *user : newGEP->users())
                repairUserType(user);
        }
        else if (auto call = dyn_cast<CallInst>(user)) {
            // Handle llvm.memcpy calls(it generated while alloc array size)
            if (auto *callee = call->getCalledFunction()) {
                if (callee->getName() == "llvm.memcpy.p0.p0.i64") {
                    errs() << "[repair memcpy] " << *call << "->";

                    auto *dest = call->getArgOperand(0);
                    auto *src = call->getArgOperand(1);
                    auto *size = call->getArgOperand(2);

                    uint64_t newSize = 0;
                    if (auto *arrayType = dyn_cast<ArrayType>(newType)) {
                        newSize = arrayType->getNumElements() * arrayType->getElementType()->getPrimitiveSizeInBits() / 8;
                    } else {
                        newSize = newType->getPrimitiveSizeInBits() / 8;
                    }

                    // Create a new constant for the new size
                    auto *newSizeConstant = ConstantInt::get(size->getType(), newSize);

                    // Create a new llvm.memcpy call with the updated parameters
                    auto *newCall = CallInst::Create(callee, {dest, src, newSizeConstant, call->getArgOperand(3)}, "", call);

                    // Replace the old call with the new one
                    call->replaceAllUsesWith(newCall);
                    errs() << *newCall << '\n';
                    instToErase.insert(call);    // erase later to avoid broken iterator
                    for (auto *user : newCall->users())
                        repairUserType(user);
                }
            }
        }
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

Instruction* ChangeVariableDecl::repairLoadInst(LoadInst* load) {
    auto *ptrOperand = load->getPointerOperand();
    if (auto *allocaInst = findOriginalAlloca(ptrOperand)) {
        if (auto *loadType = pointeeTypeMap[allocaInst]) {
            if (loadType != load->getType()) {
                Align newAlign;
                if (auto *arrayType = dyn_cast<ArrayType>(loadType)) {
                    auto *elementType = arrayType->getElementType();
                    newAlign = Align(elementType->getPrimitiveSizeInBits() / 8);
                } else if (loadType->isPointerTy()) {
                    newAlign = Align(load->getModule()->getDataLayout().getPointerPrefAlignment());
                } else {
                    newAlign = Align(loadType->getPrimitiveSizeInBits() / 8);
                }

                auto *newLoad = new LoadInst(loadType, ptrOperand, load->getName(), load->isVolatile(), load);
                newLoad->setAlignment(newAlign);
                load->replaceAllUsesWith(newLoad);
                instToErase.insert(load);

                return newLoad;
            }
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairStoreInst(StoreInst* store) {
    Value *oldValue = store->getOperand(0);
    auto *ptrOperand = store->getPointerOperand();
    if (auto *allocaInst = findOriginalAlloca(ptrOperand)) {
        auto *storeType = allocaInst->getAllocatedType();
        if (oldValue->getType() != storeType) {
            errs() << "[repair store] " << *store << "->";
            if (auto *castValue = insertCast(oldValue, storeType, store)) {
                store->setOperand(0, castValue);
                errs() << *store << '\n';
                return dyn_cast<Instruction>(store);
            }
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairUnaryOp(UnaryOperator* unaOp) {
    auto *operand = unaOp->getOperand(0);
    if (operand->getType() != unaOp->getType()) {
        errs() << "[insert cast for unary op] " << *unaOp << '\n';
        if (auto *newOperand = insertCast(operand, unaOp->getType(), unaOp)) {
            unaOp->setOperand(0, newOperand);
            return dyn_cast<Instruction>(unaOp);
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairBinaryOp(BinaryOperator* binOp) {
    bool operandsModified = false;
    SmallVector<Value*, 2> newOperands;
    auto *highestType = findHighestPrecisionType(binOp);

    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            operandsModified = true;
        }
        // type of src operands not match
        if (operand->getType() != highestType) {
            if (auto *newOperand = insertCast(operand, highestType, binOp)) {
                newOperands.push_back(newOperand);
                operandsModified = true;
            }
        } else {
            newOperands.push_back(operand);  // keep original operands
        }
    }

    if (operandsModified || binOp->getType() != highestType) {
        IRBuilder<> builder(binOp);
        auto *newBinOp = builder.CreateBinOp(binOp->getOpcode(), 
                                             newOperands[0], 
                                             newOperands[1]);
        newBinOp->takeName(binOp);
        if (auto *newBinOpInst = dyn_cast<Instruction>(newBinOp)) {
            newBinOpInst->setDebugLoc(binOp->getDebugLoc());
            replaceMultiOperandInst(binOp, newBinOpInst);
        }
        errs() << "[repair binop] " << *binOp << "->" << *newBinOp << '\n';

        return dyn_cast<Instruction>(newBinOp);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairFCmpInst(FCmpInst* fcmp) {
    bool operandsModified = false;
    SmallVector<Value*, 2> newOperands;
    auto *highestType = findHighestPrecisionType(fcmp);
    
    for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
        auto *operand = fcmp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            operandsModified = true;
        }
        if (operand->getType() != highestType) {
            if (auto *newOperand = insertCast(operand, highestType, fcmp)) {
                newOperands.push_back(newOperand);
                operandsModified = true;
            } else {
                newOperands.push_back(operand);
            }
        } else {
            newOperands.push_back(operand);
        }
    }

    if (operandsModified || fcmp->getType() != Type::getInt1Ty(fcmp->getContext())) {
        IRBuilder<> builder(fcmp);
        auto *newFcmp = builder.CreateFCmp(fcmp->getPredicate(), 
                                            newOperands[0], 
                                            newOperands[1]);
        newFcmp->takeName(fcmp);
        if (auto *newFcmpInst = dyn_cast<Instruction>(newFcmp)) {
            newFcmpInst->setDebugLoc(fcmp->getDebugLoc());
            replaceMultiOperandInst(fcmp, newFcmpInst);
        }
        errs() << "[repair FCmp] " << *fcmp << "->" << *newFcmp << '\n';

        return dyn_cast<Instruction>(newFcmp);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairCallInst(CallInst* call) {
    auto *callee = call->getCalledFunction();
    if (!callee) return nullptr;
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

Instruction* ChangeVariableDecl::repairGEPInst(GetElementPtrInst* gep, Type* baseType) {
    if (!baseType || gep->getSourceElementType() == baseType) {
        return nullptr; // 类型一致，无需替换
    }

    // 提取 GEP 指令的索引
    SmallVector<Value*, 4> indices;
    for (auto& index : gep->indices()) {
        indices.push_back(index.get());
    }

    // 创建新的 GEP 指令，使用传入的 baseType 作为基础类型
    auto* newGEP = GetElementPtrInst::CreateInBounds(
        baseType,
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

// Make inst operands type consistent
void ChangeVariableDecl::repairUserType(User *user, Type* baseType) {
    if (!isa<Instruction>(user)) {
        errs() << "[skipping unuser]\n";
        return;
    }

    auto *inst = cast<Instruction>(user);
    Instruction *newInst = nullptr;
    if (isa<FPExtInst>(inst) || isa<FPTruncInst>(inst)) {
        errs() << "[skipping check cast] " << *inst << '\n';
        return;
    }

    errs() << "[checking user] " << *inst << "\n";

    if (auto ret = dyn_cast<ReturnInst>(inst))
        newInst = repairReturnInst(ret);
    else if (auto load = dyn_cast<LoadInst>(inst))
        newInst = repairLoadInst(load);
    else if (auto store = dyn_cast<StoreInst>(inst))
        newInst = repairStoreInst(store);
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
        newInst = repairGEPInst(gep, baseType);

    if (newInst) {
        for (auto *user : newInst->users()) {
            repairUserType(user);
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
                if (castInst->use_empty()) {
                    errs() << "[remove unused cast] " << *castInst << '\n';
                    instToErase.insert(castInst);
                }
                else if (castInst->getSrcTy() == castInst->getDestTy()) {
                    errs() << "[remove redundant cast] " << *castInst << '\n';
                    castInst->replaceAllUsesWith(castInst->getOperand(0));
                    instToErase.insert(castInst);
                }
                else if (isa<FPExtInst>(castInst) || isa<FPTruncInst>(castInst)) {
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
                }

                else if (auto *bitCast = dyn_cast<BitCastInst>(&I)) {
                    repairBitCastInst(bitCast);
                }
            }
            // 若还有多操作指令没有成功修复，取消注释吃醋
            // else if (auto *binOp = dyn_cast<BinaryOperator>(&I)) {
            //     auto *highestType = findHighestPrecisionType(binOp);
            //     if (binOp->getType() != highestType) {
            //         IRBuilder<> builder(binOp);
            //         auto *newBinOp = builder.CreateBinOp(binOp->getOpcode(), 
            //                                             binOp->getOperand(0), 
            //                                             binOp->getOperand(1));
            //         newBinOp->takeName(binOp);
            //         if (auto *newBinOpInst = dyn_cast<Instruction>(newBinOp)) {
            //             newBinOpInst->setDebugLoc(binOp->getDebugLoc());
            //             replaceMultiOperandInst(binOp, newBinOpInst);
            //         }
            //         errs() << "[repair binop] " << *binOp << "->" << *newBinOp << '\n';
            //     }
            // }
            // else if (auto *fcmp = dyn_cast<FCmpInst>(&I)) {
            //     if (fcmp->getType() != Type::getInt1Ty(fcmp->getContext())) {
            //         IRBuilder<> builder(fcmp);
            //         auto *newFcmp = builder.CreateFCmp(fcmp->getPredicate(), 
            //                                             fcmp->getOperand(0), 
            //                                             fcmp->getOperand(1));
            //         newFcmp->takeName(fcmp);
            //         if (auto *newFcmpInst = dyn_cast<Instruction>(newFcmp)) {
            //             newFcmpInst->setDebugLoc(fcmp->getDebugLoc());
            //             replaceMultiOperandInst(fcmp, newFcmpInst);
            //         }
            //         errs() << "[repair FCmp] " << *fcmp << "->" << *newFcmp << '\n';
            //     }
            // }
            else if (auto *call = dyn_cast<CallInst>(&I)) {
                if (auto *callee = call->getCalledFunction()) {
                    if (callee->getName().startswith("llvm.fmuladd")) {
                        // 检查三个操作数，统计从更低精度 cast 过来的数量
                        unsigned lowPrecisionCastCount = 0;
                        SmallVector<Value*, 3> originalOperands;
                        SmallVector<Value*, 3> lowPrecisionOperands;

                        for (unsigned i = 0; i < 3; ++i) {
                            Value* operand = call->getArgOperand(i);
                            originalOperands.push_back(operand);

                            auto checkCastSrcOp = [&lowPrecisionCastCount](Value* val, Value*& original, bool needOp) {
                                if (auto castInst = dyn_cast<CastInst>(val)) {
                                    if (castInst->getOpcode() == Instruction::FPExt) {
                                        lowPrecisionCastCount++;
                                        if(needOp) original = castInst->getOperand(0);
                                    }
                                }
                            };

                            Value* originalPrecisionOperand = operand;
                            bool needOp = true;  // 是否需要获取原始操作数
                            // 尝试找到单操作数的源 Cast 操作的原始操作数类型
                            if (auto unaOp = dyn_cast<UnaryOperator>(operand)) {
                                operand = unaOp->getOperand(0);
                                needOp = false;
                            }
                            checkCastSrcOp(operand, originalPrecisionOperand, needOp);
                            lowPrecisionOperands.push_back(originalPrecisionOperand);
                        }

                        if (lowPrecisionCastCount == 3) {  // TODO: 通用版本全部3个都是低精度，才降，防止精度损失
                            // 获取原始低精度类型，假设从 FPExt 源类型获取
                            Type* lowPrecisionType = nullptr;
                            for (Value* operand : originalOperands) {
                                if (auto castInst = dyn_cast<CastInst>(operand)) {
                                    if (castInst->getOpcode() == Instruction::FPExt) {
                                        lowPrecisionType = castInst->getSrcTy();
                                        break;
                                    }
                                }
                            }

                            if (lowPrecisionType) {
                                // 构建低精度版本的函数名
                                std::string newFuncName;
                                if (lowPrecisionType->isDoubleTy()) {
                                    newFuncName = "llvm.fmuladd.f64";
                                } else if (lowPrecisionType->isFloatTy()) {
                                    newFuncName = "llvm.fmuladd.f32";
                                } else if (lowPrecisionType->isHalfTy()) {
                                    newFuncName = "llvm.fmuladd.f16";
                                } else {
                                    // 不支持的类型，跳过
                                    return;
                                }

                                // 获取低精度版本的函数
                                Function* newFunc = call->getModule()->getFunction(newFuncName);
                                if (!newFunc) {
                                    // 若函数不存在则创建
                                    FunctionType* newFuncType = FunctionType::get(
                                        lowPrecisionType, 
                                        {lowPrecisionType, lowPrecisionType, lowPrecisionType}, 
                                        false
                                    );
                                    newFunc = Function::Create(
                                        newFuncType, 
                                        GlobalValue::ExternalLinkage, 
                                        newFuncName, 
                                        call->getModule()
                                    );
                                }

                                // 确保所有操作数为低精度类型
                                for (unsigned i = 0; i < 3; ++i) {
                                    Value* operand = lowPrecisionOperands[i];
                                    if (operand->getType() != lowPrecisionType) {
                                        // 调用 insertCast 转换操作数类型
                                        if (Value* castedOperand = insertCast(operand, lowPrecisionType, call)) {
                                            lowPrecisionOperands[i] = castedOperand;
                                        } else {
                                            errs() << "Failed to cast operand " << i << " to low precision type in llvm.fmuladd call.\n";
                                            return;
                                        }
                                    }
                                }

                                // 创建新的 call 指令
                                CallInst* newCall = CallInst::Create(newFunc, lowPrecisionOperands, "", call);
                                newCall->setDebugLoc(call->getDebugLoc());
                                call->replaceAllUsesWith(newCall);
                                instToErase.insert(call);
                                errs() << "[repair fmuladd] " << *call << "->" << *newCall << '\n';

                                for (auto* user : newCall->users()) {
                                    repairUserType(user);
                                }
                            }
                        }
                    }
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
    // Check if decl inst need to be changed
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *DDI = dyn_cast<DbgDeclareInst>(&I)) {
                if (auto *DV = dyn_cast<DILocalVariable>(DDI->getVariable())) {
                    auto varName = DV->getName();
                    auto lineNo = DDI->getDebugLoc().getLine();

                    // TODO: Iterate each var to match, can be more efficient
                    for (auto &configEntry : functionConfig) {
                        if (configEntry["name"] == varName && configEntry["line"] == std::to_string(lineNo)) {
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
                                            funcArgNewTypeMap[&F][argIndex] = targetType;
                                            break;
                                        }
                                    }
                                }
                                changeAllocaInst(DDI, targetType);
                            }

                            break;  // found & changed
                        }
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


Function* ChangeVariableDecl::changeFunctionArgTypes(Function &F, DenseMap<unsigned, Type*> argNewTypeMap) {
    if (argNewTypeMap.empty()) return &F;

    FunctionType *oldFuncType = F.getFunctionType();
    SmallVector<Type*, 8> newArgTypes;
    for (unsigned i = 0; i < oldFuncType->getNumParams(); ++i) {
        if (argNewTypeMap.count(i)) {
            newArgTypes.push_back(argNewTypeMap.lookup(i));
        } else {
            newArgTypes.push_back(oldFuncType->getParamType(i));
        }
    }

    FunctionType *newFuncType = FunctionType::get(
        oldFuncType->getReturnType(),
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

    return newFunc;
}

void ChangeVariableDecl::updateCallersArgTypes(Function &oldFunc, Function &newFunc, DenseMap<unsigned, Type*>& argNewTypeMap) {
    // 遍历所有调用点
    for (User *U : oldFunc.users()) {
        if (CallInst *call = dyn_cast<CallInst>(U)) {
            for (const auto& argEntry : argNewTypeMap) {
                unsigned argIndex = argEntry.first;
                Type* targetType = argEntry.second;
                Value *oldArg = call->getArgOperand(argIndex);
                if (oldArg->getType() != targetType) {
                    if (Value *castedArg = insertCast(oldArg, targetType, call)) {
                        call->setArgOperand(argIndex, castedArg);
                    }
                }
            }
            // 替换调用的函数为新函数
            call->setCalledFunction(&newFunc);
        }
    }

    oldFunc.replaceAllUsesWith(&newFunc);
    oldFunc.eraseFromParent();
}

// Replace multi-operand Inst safely (such as BinOp, FCmpInst)
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

    // 遍历整个funcArgNewTypeMap，将其中的函数替换
    DenseMap<Function*, Function*> replaceMap;
    for (auto &funcEntry : funcArgNewTypeMap) {
        auto *functionPtr = funcEntry.first;
        auto &argNewTypeMap = funcEntry.second;
        auto newFunc = changeFunctionArgTypes(*functionPtr, argNewTypeMap);
        replaceMap[functionPtr] = newFunc;
    }

    for (auto &funcEntry : replaceMap) {
        auto *oldFunc = funcEntry.first;
        auto *newFunc = funcEntry.second;
        updateCallersArgTypes(*oldFunc, *newFunc, funcArgNewTypeMap[oldFunc]);
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