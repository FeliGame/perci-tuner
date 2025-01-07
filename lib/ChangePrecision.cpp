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

bool ChangePrecision::changeVariablePrecision(DbgDeclareInst *DDI, Type *newType) {
    auto *var = DDI->getAddress();
    auto align = Align(newType->getPrimitiveSizeInBits() / 8);
    bool modified = false;

    DenseSet<Value*> visited; // Avoid repeat change while recursive call

    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        errs() << "Changing AllocInst: " << *alloca << "\n";
        alloca->setAllocatedType(newType);
        alloca->setAlignment(align);
        errs() << "Changed AllocInst: " << *alloca << "\n";

        for (auto *user : alloca->users()) {
            if (auto store = dyn_cast<StoreInst>(user)) {
                auto *oldValue = store->getOperand(0);
                if (oldValue->getType() == newType || visited.contains(oldValue)) continue;
                if (oldValue->getType()->isFloatingPointTy() && newType->isFloatingPointTy()) {
                    if (auto *newValue = ensureTypeConsistency(oldValue, newType, store)) {
                        store->setOperand(0, newValue);
                        store->setAlignment(align);
                        modified = true;
                        visited.insert(newValue);
                    }
                }
            }
            else if (auto load = dyn_cast<LoadInst>(user)) {
                auto *oldType = load->getType();
                if (oldType == newType || visited.contains(load)) continue;

                auto *newLoad = new LoadInst(newType, load->getPointerOperand(), "", load);
                newLoad->setAlignment(align);
                if (MDNode *N = load->getMetadata("dbg")) {
                    newLoad->setMetadata("dbg", N);
                }

                load->replaceAllUsesWith(newLoad);
                instToErase.emplace_back(load);    // erase later to avoid broken iterator
                modified = true;
                visited.insert(newLoad);

                errs() << "user(newLoad): " << *newLoad << "\n";
                for (auto *loadUser : newLoad->users()) {
                    fixInstructionType(loadUser, newType, visited);
                }
            } else {
                errs() << "user(else): " << *user << "\n";
                fixInstructionType(user, newType, visited);
            }
        }
    }
    
    return modified;
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
        errs() << "repairFMulAdd(): Unsupported arg type: " << *argType << "\n";
        return false;
    }

    // Insert cast for args
    for (auto i = 0; i < 3; ++i) {
        auto *arg = call->getOperand(i);
        if (arg->getType()->getPrimitiveSizeInBits() < argType->getPrimitiveSizeInBits()) {
            errs() << "Inserting fpext for arg: " << *arg << "\n";
            args.push_back(new FPExtInst(arg, argType, "", call));
        } else if (arg->getType()->getPrimitiveSizeInBits() > argType->getPrimitiveSizeInBits()) {
            errs() << "Inserting fptrunc for arg: " << *arg << "\n";
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

    errs() << "replacing " << *call << " with " << *newCall << "\n";
    call->replaceAllUsesWith(newCall);
    instToErase.emplace_back(call);    // erase later to avoid broken iterator

    return true;
}

// Insert fpext/fptrunc cast if needed
Value *ChangePrecision::ensureTypeConsistency(Value *val, Type *targetType, Instruction *insertBefore) {
    if (val->getType() == targetType) {
        errs() << "No type cast needed for: " << *val << "\n";
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
            errs() << "Inserting fpext: " << *(val->getType()) << "->" << *targetType << "\n";
            return builder.CreateFPExt(val, targetType, "ext");
        } else if (val->getType()->getPrimitiveSizeInBits() > targetType->getPrimitiveSizeInBits()) {
            errs() << "Inserting fptrunc: " << *(val->getType()) << "->" << *targetType << "\n";
            return builder.CreateFPTrunc(val, targetType, "trunc");
        }
    } 
    else {
        errs() << "Unsupported non-floating point type cast: " 
               << *val->getType() << " -> " << *targetType << "\n";
    }

    return nullptr;
}

bool ChangePrecision::fixInstructionType(User *user, Type *targetType, DenseSet<Value*> &visited) {
    if (visited.contains(user)) {
        return false;
    }

    if (!isa<Instruction>(user)) {
        errs() << "User is not an instruction.\n";
        return false;
    }

    auto *inst = cast<Instruction>(user);

    if (isa<FPExtInst>(inst) || isa<FPTruncInst>(inst)) {
        errs() << "Skipping type cast instruction: " << *inst << "\n";
        return false;
    }

    //== 修改处：若 targetType 为空，说明我们是“泛型修复”模式，可以直接拿第一个操作数的目标类型来推
    //  也可以直接 return false，如果你只想在 "递归里" 改，而不在这里改。
    if (!targetType) {
        errs() << "No explicit targetType given, skip fixInstructionType: " << *inst << "\n";
        return false;
    }

    errs() << "Processing instruction: " << *inst << "\n";
    visited.insert(user);

    bool modified = false;

    if (auto *binOp = dyn_cast<BinaryOperator>(inst)) {
        for (unsigned i = 0; i < binOp->getNumOperands(); ++i) {
            auto *operand = binOp->getOperand(i);
            if (operand->getType() != targetType) {
                if (!visited.contains(operand)) {
                    errs() << "Operand " << i << " type " << *operand->getType() 
                           << " != target type " << *targetType << ", casting...\n";
                    if (auto *newOperand = ensureTypeConsistency(operand, targetType, binOp)) {
                        binOp->setOperand(i, newOperand);
                        modified = true;
                        visited.insert(newOperand);
                    }
                }
            }
        }

        if (binOp->getType() != targetType) {
            errs() << "binOp type " << *binOp->getType() 
                   << " != target type " << *targetType << ", repairng binOp...\n";
            IRBuilder<> builder(binOp);
            auto *newBinOp = builder.CreateBinOp(binOp->getOpcode(), 
                                                 binOp->getOperand(0), 
                                                 binOp->getOperand(1));
            newBinOp->takeName(binOp);
            if (auto *newBinOpInst = dyn_cast<Instruction>(newBinOp)) {
                newBinOpInst->setDebugLoc(binOp->getDebugLoc());
            }

            binOp->replaceAllUsesWith(newBinOp);
            instToErase.emplace_back(binOp);
            modified = true;
            visited.insert(newBinOp);

            for (auto *binOpUser : newBinOp->users()) {
                if (binOpUser->getType() != targetType) {
                    fixInstructionType(binOpUser, targetType, visited);
                }
            }
        }
    }
    // repair llvm.fmuladd
    if (auto *call = dyn_cast<CallInst>(inst)) {
        bool modified = false;
        auto *callee = call->getCalledFunction();

        if (callee && callee->getName().startswith("llvm.fmuladd")) {
            auto *longestArgType = call->getOperand(0)->getType();
            for (auto i = 1; i < 3; ++i) {  // 有4个操作数，但第四个固定为ptr，使用最高精度
                if(call->getOperand(i)->getType()->getPrimitiveSizeInBits() > longestArgType->getPrimitiveSizeInBits())
                    longestArgType = call->getOperand(i)->getType();
            }

            modified |= repairFMulAdd(call, longestArgType);        

            // for (auto *callUser : call->users()) {
            //     if (callUser->getType()!= targetType) {
            //         errs() << "Incorrect user type detected\n";
            //         fixInstructionType(callUser, targetType, visited);
            //     }
            // }
        }
    }

    // 其它指令（如 GEP、Select、FCmp 等）可在这里继续扩展
    return modified;
}

// Repair wrong fpext/fptrunc insts
bool llvm::ChangePrecision::repairCasts(Function &F)
{
    bool modified = false;

    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *fpext = dyn_cast<FPExtInst>(&I)) {
                if (fpext->getSrcTy() == fpext->getDestTy()) {
                    errs() << "Removing redundant fpext: " << *fpext << "\n";
                    fpext->replaceAllUsesWith(fpext->getOperand(0));
                    instToErase.push_back(fpext);
                    modified = true;
                }
                else if (fpext->getSrcTy()->getPrimitiveSizeInBits() > fpext->getDestTy()->getPrimitiveSizeInBits()) {
                    errs() << "Replacing incorrect fpext with fptrunc: " << *fpext << "\n";
                    IRBuilder<> builder(fpext);
                    auto *truncatedValue = builder.CreateFPTrunc(fpext->getOperand(0), fpext->getDestTy(), "trunc");
                    fpext->replaceAllUsesWith(truncatedValue);
                    instToErase.push_back(fpext);
                    modified = true;
                }
            }
            else if (auto *fptrunc = dyn_cast<FPTruncInst>(&I)) {
                if (fptrunc->getSrcTy() == fptrunc->getDestTy()) {
                    errs() << "Removing redundant fptrunc: " << *fptrunc << "\n";
                    fptrunc->replaceAllUsesWith(fptrunc->getOperand(0));
                    instToErase.push_back(fptrunc);
                    modified = true;
                }
                else if (fptrunc->getSrcTy()->getPrimitiveSizeInBits() < fptrunc->getDestTy()->getPrimitiveSizeInBits()) {
                    errs() << "Replacing incorrect fptrunc with fpext: " << *fptrunc << "\n";
                    IRBuilder<> builder(fptrunc);
                    auto *extendedValue = builder.CreateFPExt(fptrunc->getOperand(0), fptrunc->getDestTy(), "ext");
                    fptrunc->replaceAllUsesWith(extendedValue);
                    instToErase.push_back(fptrunc);
                    modified = true;
                }
            }
        }
    }

    return modified;
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
                            if (typeName == "float") {
                                targetType = Type::getFloatTy(context);
                            } else if (typeName == "double") {
                                targetType = Type::getDoubleTy(context);
                            } else if (typeName == "long double") {
                                targetType = Type::getFP128Ty(context);
                            } else {  // TODO: Add more type such as const/array/pointer...
                                errs() << "Unknown type: " << typeName << "\n";
                                continue;
                            }

                            errs() << F.getName() << ": " << varName.str() << " at line " << lineNo << typeName << '\n';
                            modified |= changeVariablePrecision(DDI, targetType);
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