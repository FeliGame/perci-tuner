// @Author: Guocheng & Guosiyuan NUDT
// Usage: Change variables' precision according to json.
// Input: Variable target type config
// Output: Precision-Changed IR

#include "ChangePrecision.h"
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

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<string> TargetConfigName("target-config", cl::value_desc("filename"), cl::desc("Target Type Config"), cl::init("config.json"));

void ChangePrecision::parseIncludedFunctions() {
    ifstream inFile("searched_functions.txt");
    string name;

    if (!inFile) {
        errs() << "Unable to open searched_functions.txt\n";
        exit(1);
    }

    while(inFile >> name) {
        includedFunctions.insert(name);
    }
}

void ChangePrecision::parseTypeConfig() {
    std::ifstream configFile(TargetConfigName.c_str());
    if (!configFile.is_open()) {
        errs() << "Failed to open " << TargetConfigName << "\n";
        return;
    }

    std::string configContent((std::istreambuf_iterator<char>(configFile)), std::istreambuf_iterator<char>());
    configFile.close();
    
    config = nlohmann::json::parse(configContent);
}

bool ChangePrecision::changeAllocaInst(DbgDeclareInst *DDI, Type *newType) {
    auto *var = DDI->getAddress();
    Align align;

    if (auto *arrayType = dyn_cast<ArrayType>(newType)) {
        auto *elementType = arrayType->getElementType();
        align = Align(elementType->getPrimitiveSizeInBits() / 8);
    } else {
        align = Align(newType->getPrimitiveSizeInBits() / 8);
    }

    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        errs() << "Changing AllocInst: " << *alloca << "\n";
        alloca->setAllocatedType(newType);
        alloca->setAlignment(align);
        errs() << "Changed AllocInst: " << *alloca << "\n";

        for (auto *user : alloca->users()) {
            if (auto store = dyn_cast<StoreInst>(user)) {
                auto *oldValue = store->getOperand(0);
                if (oldValue->getType() == newType) continue;
                if (oldValue->getType()->isFloatingPointTy() && newType->isFloatingPointTy()) {
                    if (auto *newValue = insertCast(oldValue, newType, store)) {
                        store->setOperand(0, newValue);
                        store->setAlignment(align);
                    }
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
                errs() << "erase later: " << *load << "\n";
                instToErase.insert(load);    // erase later to avoid broken iterator

                errs() << "repair users of load: " << *newLoad << "\n";
                for (auto *loadUser : newLoad->users()) {
                    if (auto ret = dyn_cast<ReturnInst>(loadUser))
                    {
                        // 获取函数的返回类型
                        auto *funcReturnType = ret->getFunction()->getReturnType();

                        // 如果返回值类型与函数返回类型不一致，插入类型转换
                        if (newType != funcReturnType) {
                            if (auto *castValue = insertCast(newLoad, funcReturnType, ret)) {
                                ret->setOperand(0, castValue);  // 设置转换后的值为返回值
                            }
                        }
                    }
                    else repairUserType(loadUser);
                }
            }
            else if (auto gep = dyn_cast<GetElementPtrInst>(user)) {
                // convert indices into ArrayRef<Value*>
                SmallVector<Value*, 4> indices;
                for (auto &index : gep->indices()) {
                    indices.push_back(index.get());
                }

                auto* newGEP = GetElementPtrInst::Create(
                    newType,
                    gep->getPointerOperand(),
                    indices,
                    gep->getName(),         // inst name
                    gep                     // inst loc
                );

                gep->replaceAllUsesWith(newGEP);
                instToErase.insert(gep);
            }
            else if (auto call = dyn_cast<CallInst>(user)) {
                // Handle llvm.memcpy calls(it generated while alloc array size)
                errs() << "call name: " << call->getName() << "\n";
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
                        errs() << "erase later: " << *call << "\n";
                        instToErase.insert(call);    // erase later to avoid broken iterator

                        errs() << "New llvm.memcpy call: " << *newCall << "\n";
                    }
                }
            }
            // else {
            //     errs() << "repair else inst's user: " << *user << "\n";
            //     repairUserType(user);
            // }
        }
        return true;
    }
    
    return false;
}

bool ChangePrecision::repairFMulAdd(CallInst *call, Type* argType) {
    SmallVector<Value*, 3> args;
    string newCallName;
    if(argType == Type::getFloatTy(call->getContext())) {
        newCallName = "llvm.fmuladd.f32";
    } else if (argType == Type::getDoubleTy(call->getContext())) {
        newCallName = "llvm.fmuladd.f64";
    } else if (argType == Type::getFP128Ty(call->getContext())) {
        newCallName = "llvm.fmuladd.f128";
    } else {
        errs() << "RepairFMulAdd(): Unsupported arg type: " << *argType << "\n";
        return false;
    }

    // Insert cast for args
    for (auto i = 0; i < 3; ++i) {
        auto *arg = call->getOperand(i);
        if (arg->getType()->getPrimitiveSizeInBits() < argType->getPrimitiveSizeInBits()) {
            args.push_back(new FPExtInst(arg, argType, "", call));
        } else if (arg->getType()->getPrimitiveSizeInBits() > argType->getPrimitiveSizeInBits()) {
            args.push_back(new FPTruncInst(arg, argType, "", call));
        } else {
            args.push_back(arg);
        }
    }

    auto newCallee = call->getCalledFunction()->getParent()->getOrInsertFunction(newCallName,
        FunctionType::get(argType, {argType, argType, argType}, false));

    auto *newCall = CallInst::Create(newCallee, args, "", call);
    newCall->takeName(call);
    newCall->setDebugLoc(call->getDebugLoc());

    errs() << "RepairFMulAdd(): replace " << *call << " with " << *newCall << "\n";
    call->replaceAllUsesWith(newCall);
    errs() << "erase later: " << *call << "\n";
    instToErase.insert(call);    // erase later to avoid broken iterator

    return true;
}

// Insert fpext/fptrunc cast if needed
Value *ChangePrecision::insertCast(Value *val, Type *targetType, Instruction *insertBefore) {
    if (val->getType() == targetType) {
        return val; 
    }

    if (auto *constantVal = dyn_cast<Constant>(val)) {
        if (constantVal->getType() == targetType) {
            return val;
        }
    }

    IRBuilder<> builder(insertBefore);

    if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        if (val->getType()->getPrimitiveSizeInBits() < targetType->getPrimitiveSizeInBits()) {
            errs() << "Insert fpext: " << *(val->getType()) << "->" << *targetType << "\n";
            return builder.CreateFPExt(val, targetType, "ext");
        } else if (val->getType()->getPrimitiveSizeInBits() > targetType->getPrimitiveSizeInBits()) {
            errs() << "Insert fptrunc: " << *val << " of type "<< *(val->getType()) << "->" << *targetType << "\n";
            return builder.CreateFPTrunc(val, targetType, "trunc");
        }
    } 
    else {
        errs() << "InsertCast(): Unsupported non-floating point type cast: " 
               << *val->getType() << " -> " << *targetType << "\n";
    }

    return nullptr;
}

// Make inst operands type consistent
bool ChangePrecision::repairUserType(User *user) {
    if (!isa<Instruction>(user)) {
        errs() << "RepairUser: User is not an inst.\n";
        return false;
    }

    auto *inst = cast<Instruction>(user);
    
    if (instRepaired.contains(inst)) return false;
    instRepaired.insert(inst);

    if (isa<FPExtInst>(inst) || isa<FPTruncInst>(inst)) {
        errs() << "RepairUser: skip cast inst: " << *inst << "\n";
        return false;
    }

    bool modified = false;

    // Find the highest precision operand type
    Type *highestType = nullptr;
    if (auto *binOp = dyn_cast<BinaryOperator>(inst)) {
        for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
            auto *operand = binOp->getOperand(i);
            if (!highestType || operand->getType()->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
                highestType = operand->getType();
            }
        }
    } else if (auto *call = dyn_cast<CallInst>(inst)) {
        auto *callee = call->getCalledFunction();
        if (callee && callee->getName().startswith("llvm.fmuladd")) {
            for (unsigned i = 0; i < 3; ++i) {  // 有4个操作数，但第四个固定为ptr，使用最高精度
                auto *operand = call->getOperand(i);
                if (!highestType || operand->getType()->getPrimitiveSizeInBits() > highestType->getPrimitiveSizeInBits()) {
                    highestType = operand->getType();
                }
            }
        }
    }

    if (auto *binOp = dyn_cast<BinaryOperator>(inst)) {
        for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
            auto *operand = binOp->getOperand(i);
            // type of src operands not match
            if (operand->getType() != highestType) {
                errs() << "RepairUser: Operand " << i << " of type " << *operand->getType() 
                       << " != target type " << *highestType<< ", casting...\n";
                if (auto *newOperand = insertCast(operand, highestType, binOp)) {
                    binOp->setOperand(i, newOperand);
                    modified = true;
                }
            }
        }
        // type of dst operand not match, replace the binOp with a new one
        if (binOp->getType() != highestType) {
            errs() << "RepairUser: binOp type not match: " << *binOp->getType() 
                   << " != target type " << *highestType << "\n";
            IRBuilder<> builder(binOp);
            auto *newBinOp = builder.CreateBinOp(binOp->getOpcode(), 
                                                 binOp->getOperand(0), 
                                                 binOp->getOperand(1));
            newBinOp->takeName(binOp);
            if (auto *newBinOpInst = dyn_cast<Instruction>(newBinOp)) {
                newBinOpInst->setDebugLoc(binOp->getDebugLoc());
            }

            binOp->replaceAllUsesWith(newBinOp);
            errs() << "erase later: " << *binOp << "\n";
            instToErase.insert(binOp);
            modified = true;

            for (auto *binOpUser : newBinOp->users()) {
                if (binOpUser->getType() != highestType) {
                    repairUserType(binOpUser);
                }
            }
        }
    }
    // repair llvm.fmuladd
    else if (auto *call = dyn_cast<CallInst>(inst)) {
        auto *callee = call->getCalledFunction();

        if (callee && callee->getName().startswith("llvm.fmuladd")) {
            modified |= repairFMulAdd(call, highestType);        
            if (modified) {
                for (auto *callUser : call->users()) {
                    if (callUser->getType()!= highestType) {
                        errs() << "repair fmuladd(): incorrect user detected.\n";
                        repairUserType(callUser);
                    }
                }
            }
        }
    }

    // 其它指令（如 GEP、Select、FCmp 等）可在这里继续扩展
    return modified;
}

// Remove redundant & Replace wrong fpext/fptrunc insts
bool llvm::ChangePrecision::repairCasts(Function &F)
{
    bool modified = false;

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *fpext = dyn_cast<FPExtInst>(&I)) {
                if (fpext->getSrcTy() == fpext->getDestTy()) {
                    errs() << "Remove redundant fpext: " << *fpext << "\n";
                    fpext->replaceAllUsesWith(fpext->getOperand(0));
                    instToErase.insert(fpext);
                    modified = true;
                }
                else if (fpext->getSrcTy()->getPrimitiveSizeInBits() > fpext->getDestTy()->getPrimitiveSizeInBits()) {
                    errs() << "Replace incorrect fpext with fptrunc: " << *fpext << "\n";
                    IRBuilder<> builder(fpext);
                    auto *truncatedValue = builder.CreateFPTrunc(fpext->getOperand(0), fpext->getDestTy(), "trunc");
                    fpext->replaceAllUsesWith(truncatedValue);
                    instToErase.insert(fpext);
                    modified = true;
                }
            }
            else if (auto *fptrunc = dyn_cast<FPTruncInst>(&I)) {
                if (fptrunc->getSrcTy() == fptrunc->getDestTy()) {
                    errs() << "Remove redundant fptrunc: " << *fptrunc << "\n";
                    fptrunc->replaceAllUsesWith(fptrunc->getOperand(0));
                    instToErase.insert(fptrunc);
                    modified = true;
                }
                else if (fptrunc->getSrcTy()->getPrimitiveSizeInBits() < fptrunc->getDestTy()->getPrimitiveSizeInBits()) {
                    errs() << "Replace incorrect fptrunc with fpext: " << *fptrunc << "\n";
                    IRBuilder<> builder(fptrunc);
                    auto *extendedValue = builder.CreateFPExt(fptrunc->getOperand(0), fptrunc->getDestTy(), "ext");
                    fptrunc->replaceAllUsesWith(extendedValue);
                    instToErase.insert(fptrunc);
                    modified = true;
                }
            }
        }
    }

    return modified;
}

// translate typename into llvm type
Type* ChangePrecision::translateTypeName(string typeName, LLVMContext& context)
{
    Type* targetType = nullptr;
    if (typeName.find("float") == 0) {
        targetType = Type::getFloatTy(context);
    } else if (typeName.find("double") == 0) {
        targetType = Type::getDoubleTy(context);
    } else if (typeName.find("long double") == 0) {
        targetType = Type::getFP128Ty(context);
    } else {  // TODO: Add more type such as const/array/pointer...
        errs() << "Unknown type: " << typeName << "\n";
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

bool ChangePrecision::runOnFunction(Function &F) {
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
                            Type *targetType = nullptr;
                            auto typeName = configEntry["type"].get<string>();

                            if (typeName == parseDITypeName(DV->getType())) continue;
                            targetType = translateTypeName(typeName, context);
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

    modified |= repairCasts(F);
    return modified;
}

bool ChangePrecision::runOnModule(Module &module) {
    parseIncludedFunctions();
    parseTypeConfig();
    bool modified = false;
    for(auto f = module.begin(), fe = module.end(); f != fe; f++) {
        string name = f->getName().str();
        if(!f->isDeclaration() && includedFunctions.find(name) != includedFunctions.end()) {
            modified |= runOnFunction(*f);
        }
    }

    for (auto* inst : instToErase) {
        inst->eraseFromParent();
    }

    return modified;
}  

void ChangePrecision::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll();
}

PreservedAnalyses ChangePrecision::run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << ">>> Perci-Tuner: Running ChangePrecision Pass...\n";    
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}