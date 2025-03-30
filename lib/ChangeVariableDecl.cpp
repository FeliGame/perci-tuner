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
            if (MDNode *N = load->getMetadata("dbg")) {
                newLoad->setMetadata("dbg", N);
            }

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
        if (constantVal->getType() == targetType)
            return val;
        
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
    
    errs() << "InsertCast(): Unsupported non-floating point type cast: " 
           << *val->getType() << " -> " << *targetType << "\n";
    return nullptr;
}

Type* ChangeVariableDecl::findHighestPrecisionType(BinaryOperator *binOp) {
    Type *highestType = nullptr;
    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
        if (!highestType || operand->getType()->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
            highestType = operand->getType();
        }
    }
    return highestType;
}

// Same as BinOp
Type* ChangeVariableDecl::findHighestPrecisionType(FCmpInst *fcmp) {
    Type *highestType = nullptr;
    for (unsigned i = 0; i < fcmp->getNumOperands(); ++i) {
        auto *operand = fcmp->getOperand(i);
        if (!highestType || operand->getType()->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
            highestType = operand->getType();
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
    
    for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
        auto *operand = binOp->getOperand(i);
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
        }

        binOp->replaceAllUsesWith(newBinOp);
        instToErase.insert(binOp);
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
        }

        fcmp->replaceAllUsesWith(newFcmp);
        instToErase.insert(fcmp);
        return dyn_cast<Instruction>(newFcmp);
    }

    return nullptr;
}

Instruction* ChangeVariableDecl::repairCallInst(CallInst* call) {
    auto *callee = call->getCalledFunction();
    if (callee && !callee->isVarArg()) {  // Ignore variadic args' call 
        bool modified = false;
        SmallVector<Type *, 4> newParamTypes;
        SmallVector<Value *, 4> newArgs;

        unsigned numArgs = callee->getFunctionType()->getNumParams(); // Subtract 1 for the function itself

        // Check if any argument type mismatch with the function parameter type
        for (unsigned i = 0; i < numArgs; ++i) {
            Value *arg = call->getArgOperand(i);
            Type *paramType = callee->getFunctionType()->getParamType(i);

            if (arg->getType() != paramType) {
                modified = true;
                // Insert a cast to convert the argument to the expected type
                if (auto *castValue = insertCast(arg, paramType, call)) {
                    newArgs.push_back(castValue); // Use the casted value
                } else {
                    newArgs.push_back(arg); // Use the original argument if cast fails
                    errs() << "Failed to cast argument " << i;
                }
            } else {
                newArgs.push_back(arg); // Use the original argument
            }
        }

        if (modified) {
            CallInst *newCall = CallInst::Create(callee, newArgs, "", call);
            newCall->takeName(call);
            newCall->setCallingConv(call->getCallingConv());
            newCall->setAttributes(call->getAttributes());
            newCall->setDebugLoc(call->getDebugLoc());

            call->replaceAllUsesWith(newCall);
            instToErase.insert(call);
            errs() << "Repaired call instruction with cast: " << *newCall << "\n";
            return dyn_cast<Instruction>(newCall);
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
                if (!castInst->getSrcTy()->isFloatingPointTy() || 
                    !castInst->getDestTy()->isFloatingPointTy()) 
                    continue;

                if (castInst->getSrcTy() == castInst->getDestTy()) {
                    errs() << "Remove redundant cast: " << *castInst << "\n";
                    castInst->replaceAllUsesWith(castInst->getOperand(0));
                    instToErase.insert(castInst);
                    modified = true;
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

    modified |= repairCasts(F);
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
        // Match a struct decl with config
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
                                            // Set align, copy metadata
                                            newLoad->setAlignment(Align(maxAlign));
                                            if (MDNode *N = load->getMetadata("dbg")) {
                                                newLoad->setMetadata("dbg", N);
                                            }
                                            
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

    for (auto *inst : instToErase) {
        inst->eraseFromParent();
    }

    // module.print(outs(), nullptr);
    if (verifyModule(module, &errs()))
    {
        errs() << "Module verify failed!\n";
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