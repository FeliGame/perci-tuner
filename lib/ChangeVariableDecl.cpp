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

#include <fstream>

#define CF1(I, s) changeToDDCall(I, s, {I->getOperand(0)}, I->getType())
#define CF2(I, s) changeToDDCall(I, s, {I->getOperand(0), I->getOperand(1)}, I->getType())
#define CF3(I, s) changeToDDCall(I, s, {I->getOperand(0), I->getOperand(1), I->getOperand(2)}, I->getType())
#define CB2(I, s) changeToDDCall(I, s, {I->getOperand(0), I->getOperand(1)}, Type::getInt1Ty(I->getModule()->getContext()))
#define CT1(I, s) changeToDDCall(I, s, {I->getOperand(0)}, I->getType())

using namespace std;
using namespace llvm;


cl::opt<string> VariableConfigName("variable-config", cl::value_desc("filename"), cl::desc("Variable's Type Config"), cl::init("config.json"));
cl::opt<bool> FP128ToDDFlag("fp128-to-dd", cl::desc("Change fp128 to dd"), cl::init(false));

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

bool ChangeVariableDecl::changeAllocaInst(DbgDeclareInst *DDI, Type *newType) {
    auto *var = DDI->getAddress();
    Align align;

    if (auto *arrayType = dyn_cast<ArrayType>(newType)) {
        auto *elementType = arrayType->getElementType();
        align = Align(elementType->getPrimitiveSizeInBits() / 8);
    } else {
        align = Align(newType->getPrimitiveSizeInBits() / 8);
    }

    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        IRBuilder<> builder(alloca);
        auto *newAlloca = builder.CreateAlloca(newType, nullptr, alloca->getName());
        newAlloca->setAlignment(align);
        newAlloca->takeName(alloca);
        newAllocas.insert(newAlloca);
        instToErase.insert(alloca);
        alloca->replaceAllUsesWith(newAlloca);
        return true;
    }
    return false;
}

void ChangeVariableDecl::repairAllocaUsers(AllocaInst* alloca) {
    auto newType = alloca->getAllocatedType();
    Align align;

    if (auto *arrayType = dyn_cast<ArrayType>(newType)) {
        auto *elementType = arrayType->getElementType();
        align = Align(elementType->getPrimitiveSizeInBits() / 8);
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
                    errs() << "Changing llvm.memcpy call: " << *call << "\n";

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
                    instToErase.insert(call);    // erase later to avoid broken iterator
                    for (auto *user : newCall->users())
                        repairUserType(user);

                    errs() << "New llvm.memcpy call: " << *newCall << "\n";
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

    if (auto *constantVal = dyn_cast<Constant>(val)) {
        if (targetType->isFloatingPointTy()) {
            return ConstantExpr::getFPCast(constantVal, targetType);
        }
        return nullptr;
    }

    IRBuilder<> builder(insertBefore);

    if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        errs() << "Insert FP cast: " << *val->getType() << "->" << *targetType << "\n";
        return builder.CreateFPCast(val, targetType, "fp_cast");
    }
    if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy()) {
        errs() << "Insert IntToFP cast: " << *val->getType() << "->" << *targetType << "\n";
        return builder.CreateSIToFP(val, targetType, "int_to_fp");
    }
    if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy()) {
        errs() << "Insert FPToInt cast: " << *val->getType() << "->" << *targetType << "\n";
        return builder.CreateFPToSI(val, targetType, "fp_to_int");
    }
    
    errs() << "InsertCast(): Unsupported non-floating point type cast: " 
           << *val->getType() << " -> " << *targetType << "\n";
    return nullptr;
}

Type* ChangeVariableDecl::findHighestPrecisionType(BinaryOperator *binOp) {
    Type *highestType = nullptr;
    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
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

Instruction* ChangeVariableDecl::repairStoreInst(StoreInst* store) {
    Value *oldValue = store->getOperand(0);
    auto *ptrOperand = store->getPointerOperand();
    if (auto *allocaInst = findOriginalAlloca(ptrOperand)) {
        auto *storeType = allocaInst->getAllocatedType();
        errs() << "New store type: " << *storeType << "\n";
        if (oldValue->getType() != storeType) {
            if (auto *castValue = insertCast(oldValue, storeType, store)) {
                store->setOperand(0, castValue);
                return dyn_cast<Instruction>(store);
            }
        }
    }
    return nullptr;
}

Instruction* ChangeVariableDecl::repairUnaryOp(UnaryOperator* unaOp) {
    auto *operand = unaOp->getOperand(0);
    if (operand->getType() != unaOp->getType()) {
        errs() << "RepairUser: Unary operand type mismatch\n";
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
    
    if(FP128ToDDFlag) {
        unsigned convertToDD = 0;
        for (unsigned i = 0; i < binOp->getNumOperands(); ++i)
        {
            if (binOp->getOperand(i)->getType()->isFP128Ty())
            {
                ++convertToDD;
            }
            else if (binOp->getOperand(i)->getType()->isDoubleTy())
            {
                ++convertToDD;
            }
        }
        if (convertToDD == 2) {
            return binOp;  // 保持原样，不插入cast（交给后续handle统一执行）
        }
    }

    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            operandsModified = true;
        }
        // type of src operands not match
        if (operand->getType() != highestType) {
            errs() << "RepairUser: Binary operand " << i << " of type " << *operand->getType() 
                   << " != target type " << *highestType<< ", casting...\n";
            if (auto *newOperand = insertCast(operand, highestType, binOp)) {
                newOperands.push_back(newOperand);
                operandsModified = true;
            }
        } else {
            newOperands.push_back(operand);  // keep original operands
        }
    }

    if (operandsModified || binOp->getType() != highestType) {
        errs() << "RepairUser: binOp type mismatch\n";            
        IRBuilder<> builder(binOp);
        auto *newBinOp = builder.CreateBinOp(binOp->getOpcode(), 
                                             newOperands[0], 
                                             newOperands[1]);
        newBinOp->takeName(binOp);
        if (auto *newBinOpInst = dyn_cast<Instruction>(newBinOp)) {
            newBinOpInst->setDebugLoc(binOp->getDebugLoc());
            updateMultiOpReplacement(binOp, newBinOpInst);
        }

        return dyn_cast<Instruction>(newBinOp);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairFCmpInst(FCmpInst* fcmp) {
    bool operandsModified = false;
    SmallVector<Value*, 2> newOperands;
    auto *highestType = findHighestPrecisionType(fcmp);
    
    if(FP128ToDDFlag) {
        unsigned convertToDD = 0;
        for (unsigned i = 0; i < fcmp->getNumOperands(); ++i)
        {
            if (fcmp->getOperand(i)->getType()->isFP128Ty())
            {
                ++convertToDD;
            }
            else if (fcmp->getOperand(i)->getType()->isDoubleTy())
            {
                ++convertToDD;
            }
        }
        if (convertToDD == 2) {
            return fcmp;  // 保持原样，不插入cast（交给后续handle统一执行）
        }
    }
    
    for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
        auto *operand = fcmp->getOperand(i);
        // if typecast, use src value
        if (auto *castInst = dyn_cast<CastInst>(operand)) {
            operand = castInst->getOperand(0);
            operandsModified = true;
        }
        if (operand->getType() != highestType) {
            errs() << "RepairUser: FCmp operand " << i << " of type " << *operand->getType() 
                << " != target type " << *highestType << ", casting...\n";
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
        errs() << "RepairUser: FCmp type mismatch\n";
        IRBuilder<> builder(fcmp);
        auto *newFcmp = builder.CreateFCmp(fcmp->getPredicate(), 
                                            newOperands[0], 
                                            newOperands[1]);
        newFcmp->takeName(fcmp);
        if (auto *newFcmpInst = dyn_cast<Instruction>(newFcmp)) {
            newFcmpInst->setDebugLoc(fcmp->getDebugLoc());
            updateMultiOpReplacement(fcmp, newFcmpInst);
        }

        return dyn_cast<Instruction>(newFcmp);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairCallInst(CallInst* call) {
    auto *callee = call->getCalledFunction();
    if (callee && !callee->isVarArg()) {  // Ignore variadic args' call 
        bool modified = false;
        // Check if any argument type mismatch with the function parameter type
        for (unsigned i = 0; i < callee->getFunctionType()->getNumParams(); ++i) {
            Value *arg = call->getArgOperand(i);
            Type *paramType = callee->getFunctionType()->getParamType(i);

            if (arg->getType() != paramType) {
                if (auto *castValue = insertCast(arg, paramType, call)) {
                    call->setArgOperand(i, castValue);
                    modified = true;
                } else {
                    errs() << "Failed to cast argument " << i;
                }
            }
        }

        if (modified) {
            errs() << "Repaired call instruction with cast: " << *call << "\n";
            return call;
        }
    }

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
                errs() << "RepairUser: PHI operand " << i << " of type " << *oldVal->getType()
                    << "!= target type " << *highestType << ", casting...\n";
                auto *incomingBlock = phi->getIncomingBlock(i);
                auto *insertBefore = incomingBlock->getTerminator();
                newVal = insertCast(oldVal, highestType, insertBefore);
            }

            newPhi->addIncoming(newVal, phi->getIncomingBlock(i));
        }
        phi->replaceAllUsesWith(newPhi);
        instToErase.insert(phi);
        return dyn_cast<Instruction>(newPhi);
    }

    return nullptr;
}

// Make inst operands type consistent
bool ChangeVariableDecl::repairUserType(User *user) {
    if (!isa<Instruction>(user)) {
        errs() << "RepairUser: User is not an inst.\n";
        return false;
    }

    auto *inst = cast<Instruction>(user);
    Instruction *newInst = nullptr;
    if (isa<FPExtInst>(inst) || isa<FPTruncInst>(inst)) {
        errs() << "RepairUser: skip cast inst: " << *inst << "\n";
        return false;
    }

    errs() << "RepairUser: " << *inst << "\n";

    if (auto ret = dyn_cast<ReturnInst>(inst))
        newInst = repairReturnInst(ret);
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

    if (newInst) {
        for (auto *user : newInst->users()) {
            repairUserType(user);
        }
        return true;
    }

    return false;
}

// Remove redundant & Replace wrong fpext/fptrunc insts
bool llvm::ChangeVariableDecl::repairCasts(Function &F)
{
    bool modified = false;
    IRBuilder<> builder(F.getContext());

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *castInst = dyn_cast<CastInst>(&I)) {
                if (castInst->use_empty()) {
                    errs() << "Remove unused cast: " << *castInst << "\n";
                    instToErase.insert(castInst);
                    modified = true;
                    continue;
                }

                if (castInst->getSrcTy() == castInst->getDestTy()) {
                    errs() << "Remove redundant cast: " << *castInst << "\n";
                    castInst->replaceAllUsesWith(castInst->getOperand(0));
                    instToErase.insert(castInst);
                    modified = true;
                    continue;
                }

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
                        modified = true;
                        continue;
                    }
                    // 如果目标类型精度高于源类型，且当前是FPTrunc，则改为FPExt
                    else if (destType->getPrimitiveSizeInBits() > srcType->getPrimitiveSizeInBits() && 
                             isa<FPTruncInst>(castInst)) {
                        builder.SetInsertPoint(castInst);
                        auto *newCast = builder.CreateFPExt(castInst->getOperand(0), destType);
                        castInst->replaceAllUsesWith(newCast);
                        instToErase.insert(castInst);
                        modified = true;
                        continue;
                    }
                }
            }
        }
    }

    return modified;
}

// find the original AllocaInst of given inst
AllocaInst* ChangeVariableDecl::findOriginalAlloca(Value* value) {
    while (true) {
        if (auto *alloca = dyn_cast<AllocaInst>(value)) {
            return alloca;
        }
        if (auto *gep = dyn_cast<GetElementPtrInst>(value)) {
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

// Change Inst to DD CallInst
template <typename T>
Instruction* ChangeVariableDecl::changeToDDCall(T *I, const std::string &FunctionName, const std::vector<Value *> &Args, Type *ReturnType)
{
    Instruction *Inst = dyn_cast<Instruction>(I);
    Module *M = Inst->getModule();
    IRBuilder<> Builder(I);

    std::vector<Value *> newArgs;
    if (!(dyn_cast<StoreInst>(I)))
    {
        for (unsigned i = 0; i < Args.size(); i++)
        {
            auto *arg = Inst->getOperand(i);
            auto *constant = dyn_cast<Constant>(arg);
            if (constant && arg->getType()->isFP128Ty())
            { // 如果函数参数中有fp128常量，先转成dd
                Function *FP128ToDD = M->getFunction("FP128ToDD");
                if (!FP128ToDD)
                {
                    Type *fp128Type = Type::getFP128Ty(M->getContext());
                    FunctionType *funType = FunctionType::get(Type::getVoidTy(M->getContext()), {fp128Type, fp128Type->getPointerTo()}, false);
                    FP128ToDD = Function::Create(funType, Function::ExternalLinkage, "FP128ToDD", M);
                }
                AllocaInst *alloca = Builder.CreateAlloca(constant->getType());
                Builder.CreateCall(FP128ToDD, {constant, alloca});
                LoadInst *load = Builder.CreateLoad(arg->getType(), alloca);
                newArgs.push_back(load);
            }
            else
            {
                newArgs.push_back(arg);
            }
        }
    }
    else
    {
        newArgs = Args;
    }

    std::vector<Type *> aType;
    for (auto *arg : Args)
    {
        aType.push_back(arg->getType());
    }
    FunctionType *FuncTy = FunctionType::get(ReturnType, aType, false);
    Function *Callee = M->getFunction(FunctionName);
    if (!Callee || Callee->getFunctionType() != FuncTy)
    {
        Callee = Function::Create(FuncTy, Function::ExternalLinkage, FunctionName, M);
    }
    CallInst *Call = Builder.CreateCall(Callee, newArgs);
    Call->setDebugLoc(I->getDebugLoc());

    I->replaceAllUsesWith(Call);
    instToErase.insert(I);

    return Call;
}

// replace version of binOp for double-double
std::string ChangeVariableDecl::getDDBinOpCallName(BinaryOperator *bo)
{
    switch (bo->getOpcode())
    {
    case Instruction::FAdd:
        return "fp128_add";
    case Instruction::FSub:
        return "fp128_sub";
    case Instruction::FMul:
        return "fp128_mul";
    case Instruction::FDiv:
        return "fp128_div";
    default:
        return "";
    }
}

void ChangeVariableDecl::handleFP128ToDD(Instruction& I) {
    if (auto *SI = dyn_cast<StoreInst>(&I))
    {
        if (SI->getValueOperand()->getType()->isFP128Ty())
        {
            // Get the fp128 constant
            auto *FP128Const = dyn_cast<ConstantFP>(SI->getValueOperand());
            if (!FP128Const)
                return;
            changeToDDCall(SI, "FP128ToDD", {SI->getValueOperand(), SI->getPointerOperand()}, SI->getType());
        }
    }
    else if (auto *BinOp = dyn_cast<BinaryOperator>(&I))
    { // 处理计算指令
        std::string funName = getDDBinOpCallName(BinOp);
        if (BinOp->getType()->isFP128Ty() && !funName.empty())
        { // 处理fp128的add,sub,mul,div
            for (unsigned i = 0; i < BinOp->getNumOperands(); ++i)
            {
                if (BinOp->getOperand(i)->getType()->isFP128Ty())
                {
                    funName += "2";
                }
                else if (BinOp->getOperand(i)->getType()->isDoubleTy())
                {
                    funName += "1";
                }
            }
            CF2(BinOp, funName);
        }
    }
    else if (I.getOpcode() == Instruction::FNeg)
    {
        Instruction *II = &I;
        CF1(II, "fp128_neg");
    }
    else if (auto *FCmp = dyn_cast<FCmpInst>(&I))
    { // 处理比较指令
        if (FCmp->getOperand(0)->getType()->isFP128Ty())
        {
            switch (FCmp->getPredicate())
            {
            case FCmpInst::FCMP_OGT:
            case FCmpInst::FCMP_UGT:
                CB2(FCmp, "fp128_gt");
                break;
            case FCmpInst::FCMP_OGE:
            case FCmpInst::FCMP_UGE:
                CB2(FCmp, "fp128_ge");
                break;
            case FCmpInst::FCMP_OLT:
            case FCmpInst::FCMP_ULT:
                CB2(FCmp, "fp128_lt");
                break;
            case FCmpInst::FCMP_OLE:
            case FCmpInst::FCMP_ULE:
                CB2(FCmp, "fp128_le");
                break;
            case FCmpInst::FCMP_OEQ:
            case FCmpInst::FCMP_UEQ:
                CB2(FCmp, "fp128_eq");
                break;
            case FCmpInst::FCMP_ONE:
            case FCmpInst::FCMP_UNE:
                CB2(FCmp, "fp128_ne");
                break;
            default:
                return;
            }
        }
    }
    else if (auto *FCall = dyn_cast<CallInst>(&I))
    { // 处理函数调用
        auto *Callee = FCall->getCalledFunction();
        if (Callee)
        {
            if (Callee->getReturnType()->isFP128Ty())
            {
                if (Callee->getName() == "llvm.fmuladd.f128")
                {
                    std::string funName = "fp128_muladd";
                    for (unsigned i = 0; i < FCall->getNumOperands(); ++i)
                    {
                        if (FCall->getOperand(i)->getType()->isFP128Ty())
                        {
                            funName += "2";
                        }
                        else if (FCall->getOperand(i)->getType()->isDoubleTy())
                        {
                            funName += "1";
                        }
                    }
                    CF3(FCall, funName);
                }
                else if (Callee->getName() == "sinl")
                {
                    CF1(FCall, "fp128_sin");
                }
                else if (Callee->getName() == "acosl")
                {
                    CF1(FCall, "fp128_acos");
                }
                else if (Callee->getName() == "sqrtl")
                {
                    CF1(FCall, "fp128_sqrt");
                }
                else if (Callee->getName() == "expl")
                {
                    CF1(FCall, "fp128_exp");
                }
            }
            if (Callee->getName() == "printf" && Callee->getFunctionType()->isVarArg())
            {
                for (unsigned i = 0; i < FCall->getNumOperands(); ++i)
                {
                    auto *pa = FCall->getOperand(i);
                    if (pa->getType()->isFP128Ty())
                    {
                        IRBuilder<> Builder(&I);
                        Function *Fun = I.getModule()->getFunction("fp128_printdd");
                        if (!Fun)
                        {
                            Fun = Function::Create(
                                FunctionType::get(Type::getInt32Ty(I.getContext()), {pa->getType()}, false),
                                Function::ExternalLinkage, "fp128_printdd", I.getModule());
                        }
                        auto *newPa = Builder.CreateCall(Fun, pa);
                        newPa->setDebugLoc(I.getDebugLoc());
                        I.replaceAllUsesWith(newPa);
                        instToErase.insert(&I);
                        break;
                    }
                }
            }
        }
    }
    else if (auto *CI = dyn_cast<CastInst>(&I))
    {
        Type *SrcType = CI->getSrcTy();
        Type *DstType = CI->getDestTy();
        switch (CI->getOpcode())
        {
        case Instruction::FPTrunc:
			if (SrcType->isFP128Ty() && DstType->isDoubleTy())
			{
				CT1(CI, "DDToDouble");
			}
			else if (SrcType->isFP128Ty() && DstType->isFloatTy())
			{
				CT1(CI, "DDToFloat");
			} /*else if (SrcType->isFloatTy() && DstType->isBFloatTy()) {
				CT1(CI, "FP32ToBF16");
			} else if (SrcType->isDoubleTy() && DstType->isBFloatTy()) {
				CT1(CI, "FP64ToBF16");
			}*/
			else
			{
				// errs() << "Don't neet to change.\n";
			}
			break;

		case Instruction::FPExt:
			if (SrcType->isDoubleTy() && DstType->isFP128Ty())
			{
				CT1(CI, "DoubleToDD");
			}
			else if (SrcType->isFloatTy() && DstType->isFP128Ty())
			{
				CT1(CI, "FloatToDD");
			} /*else if (SrcType->isBFloatTy() && DstType->isFloatTy()) {
				CT1(CI, "BF16ToFP32");
			} else if (SrcType->isBFloatTy() && DstType->isDoubleTy()) {
				CT1(CI, "BF16ToFP64");
			} */
			else
			{
				// errs() << "Don't neet to change.\n";
			}
			break;

		case Instruction::FPToUI:
			if (SrcType->isFP128Ty() && DstType->isIntegerTy())
			{
				if (auto *intTy = dyn_cast<IntegerType>(DstType))
				{
					unsigned bw = intTy->getIntegerBitWidth();
					if (bw == 32)
					{
						CT1(CI, "DDToU32");
					}
					else if (bw == 64)
					{
						CT1(CI, "DDToU64");
					}
				}
			}
			break;

		case Instruction::FPToSI:
			if (SrcType->isFP128Ty() && DstType->isIntegerTy())
			{
				if (auto *intTy = dyn_cast<IntegerType>(DstType))
				{
					unsigned bw = intTy->getIntegerBitWidth();
					if (bw == 32)
					{
						CT1(CI, "DDToS32");
					}
					else if (bw == 64)
					{
						CT1(CI, "DDToS64");
					}
				}
			}
			break;

		case Instruction::UIToFP:
			if (SrcType->isIntegerTy() && DstType->isFP128Ty())
			{
				if (auto *intTy = dyn_cast<IntegerType>(SrcType))
				{
					unsigned bw = intTy->getIntegerBitWidth();
					if (bw == 32)
					{
						CT1(CI, "U32ToDD");
					}
					else if (bw == 64)
					{
						CT1(CI, "U64ToDD");
					}
				}
			}
			break;

		case Instruction::SIToFP:
			if (SrcType->isIntegerTy() && DstType->isFP128Ty())
			{
				if (auto *intTy = dyn_cast<IntegerType>(SrcType))
				{
					unsigned bw = intTy->getIntegerBitWidth();
					if (bw == 32)
					{
						CT1(CI, "S32ToDD");
					}
					else if (bw == 64)
					{
						CT1(CI, "S64ToDD");
					}
				}
			}
			break;

		default:
			// BitCast Trunc ZExt SExt PtrToInt IntToPtr
			break;
		}
    }
}

bool ChangeVariableDecl::runOnFunction(Function &F) {
    auto &context = F.getContext();
    auto functionConfig = config["local"][demangle(F.getName())];
    bool modified = false;
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

                            if (typeName == parseDITypeName(DV->getType())) continue;
                            auto *targetType = translateValidTypeName(typeName, context);
                            if (targetType == nullptr) continue;

                            errs() << F.getName() << ": " << varName.str() << " at line " << lineNo << typeName << '\n';
                            modified |= changeAllocaInst(DDI, targetType);
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

    removeRedundantReplacements();

    for (auto *inst : instToErase) {
        inst->eraseFromParent();
    }
    instToErase.clear();

    modified |= repairCasts(F);

    if(FP128ToDDFlag)
    {
        for (auto &BB : F)
        {
            for(auto &I: BB)
            {
                handleFP128ToDD(I);
            }
        }
    }

    for (auto *inst : instToErase) {
        inst->eraseFromParent();
    }
    instToErase.clear();
    return modified;
}

bool ChangeVariableDecl::changeStructDeclarations(Module &module) {
    if (!config.contains("structs")) return false;

    bool modified = false;
    nlohmann::json structInfos = config["structs"];
    auto &context = module.getContext();
    // Iterate all struct decls in the module
    for (auto *sty : module.getIdentifiedStructTypes()) {
        StringRef rawName = sty->getName();
        StringRef structName = rawName.startswith("struct.") ? rawName.substr(7) : rawName;
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
                    errs() << "Change struct member type: " << *newType << " -> " << *targetType << "\n";
                    newType = targetType;
                    needsUpdate = true;
                }
            
                newElements.push_back(newType);
            }

            if (needsUpdate) {
                sty->setBody(newElements, sty->isPacked());
                modified = true;
                
                // Replace global/static initializers
                for (auto &global : module.globals()) {
                    if (!global.hasInitializer()) continue;
                    if (auto init = dyn_cast<ConstantStruct>(global.getInitializer())) {
                        if (init->getType() == sty) {
                            errs() << "Repair global/static struct initializers: " << *init << " -> " << *sty << "\n";
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
    return modified;
}

// Select available multi-operand Inst (such as BinOp, FCmpInst) and remove other replacements
void ChangeVariableDecl::updateMultiOpReplacement(Instruction* oldInst, Instruction* newInst) {
    if (instReplaceMap.count(oldInst))  // has other replacement, replace uses & remove before
    {
        auto *beforeInst = instReplaceMap[oldInst];
        beforeInst->replaceAllUsesWith(newInst);
        instToErase.insert(beforeInst);
        instReplaceMap.erase(beforeInst);
    } else {  // no replacement, just replace uses
        oldInst->replaceAllUsesWith(newInst);
        instReplaceMap[oldInst] = dyn_cast<Instruction>(newInst);
        instToErase.insert(oldInst);
    }
}

// Remove insts without users, they are from MultiOperandInsts (BinOp, FCmpInst)
void ChangeVariableDecl::removeRedundantReplacements() {
    for (auto &pair : instReplaceMap) {
        auto *candidate = pair.second;
        if (candidate->use_empty()) {
            errs() << "Remove redundant inst: " << *candidate << "\n";
            instToErase.insert(candidate);
        }
    }
    instReplaceMap.clear();
}

bool ChangeVariableDecl::runOnModule(Module &module) {
    parseConfig();
    bool modified = false;
    modified |= changeStructDeclarations(module);
    for(auto f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            modified |= runOnFunction(*f);
        }
    }

    if (verifyModule(module, &errs()))
    {
        errs() << "Module verify failed, IR as below:\n";
        module.print(errs(), nullptr);
        errs() << "<<< IR OVER <<<\n";
        exit(1);
    } else {
        errs() << "Module verify success!\n";
    }

    return modified;
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