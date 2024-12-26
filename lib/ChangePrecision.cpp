// @Author: Guocheng - NUDT
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

// 【Temporary】
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
    Value *var = DDI->getAddress();
    auto align = Align(newType->getPrimitiveSizeInBits() / 8);
    bool modified = false;

    if (auto alloca = dyn_cast<AllocaInst>(var)) {
        errs() << "Changing AllocInst: " << *alloca << "\n";
        alloca->setAllocatedType(newType);
        alloca->setAlignment(align);
        errs() << "Changed AllocInst: " << *alloca << "\n";

        for (User *user : alloca->users()) {
            if (auto store = dyn_cast<StoreInst>(user)) {
                // 获取原始的赋值
                Value *oldValue = store->getOperand(0);

                // 将原始的赋值转换为新的类型
                Value *newValue = nullptr;
                // only converting float/double/long double
                if (oldValue->getType()->isFloatingPointTy() && newType->isFloatingPointTy()) {
                    auto valueAPF = dyn_cast<ConstantFP>(oldValue)->getValueAPF();
                    if (newType->isFloatTy()) {
                        newValue = ConstantFP::get(newType, valueAPF.convertToFloat());
                    } else if (newType->isDoubleTy()) {
                        newValue = ConstantFP::get(newType, valueAPF.convertToDouble());
                    // } else if (newType->isFP128Ty()) {
                    //     newValue = ConstantFP::get(newType, valueAPF.convert(APFloat::IEEEquad(), APFloat::rmNearestTiesToEven));
                    } else {
                        errs() << "Unsupported new type: " << *newType << "\n";
                        continue;
                    }
                } else {
                    errs() << "Unsupported old type: " << *oldValue->getType() << "\n";
                    continue;
                }

                store->setOperand(0, newValue);
                store->setAlignment(align);
                modified = true;
            }
            else if (auto load = dyn_cast<LoadInst>(user)) {
                Type *oldType = load->getType();
                if (oldType == newType) continue;

                auto *newLoad = new LoadInst(newType, load->getPointerOperand(), "", load);
                newLoad->setAlignment(align);
                if (MDNode *N = load->getMetadata("dbg")) {
                    newLoad->setMetadata("dbg", N);
                }

                load->replaceAllUsesWith(newLoad);
                instToErase.emplace_back(load);    // erase later to avoid broken iterator
                modified = true;
            }
        }
    }
    
    return modified;
}

bool ChangePrecision::changeCallPrecision(CallInst *call, const std::string &newCallName) {
    // 获取当前调用指令的参数列表
    SmallVector<Value*, 3> args;
    for (unsigned i = 0; i < 3; ++i) {
        Value *arg = call->getOperand(i);
        if (arg->getType()->isFloatTy()) {
            // 将float类型的参数转换为double类型
            args.push_back(new FPExtInst(arg, Type::getDoubleTy(arg->getContext()), "", call));
        } else {
            args.push_back(arg);
        }
    }

    // 创建新的调用指令
    FunctionCallee newCallee = call->getCalledFunction()->getParent()->getOrInsertFunction(newCallName,
        FunctionType::get(Type::getDoubleTy(call->getContext()),
            {Type::getDoubleTy(call->getContext()),
             Type::getDoubleTy(call->getContext()),
             Type::getDoubleTy(call->getContext())}, false));

    auto *newCall = CallInst::Create(newCallee, args, "", call);
    newCall->takeName(call);
    newCall->setDebugLoc(call->getDebugLoc());

    // 替换原来的调用指令
    errs() << "replacing " << *call << " with " << *newCall << "\n";
    call->replaceAllUsesWith(newCall);
    instToErase.emplace_back(call);    // erase later to avoid broken iterator

    return true;
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
                    unsigned lineNo = DDI->getDebugLoc().getLine();

                    // TODO: Iterate each var to match, can be more efficient
                    for (auto &configEntry : functionConfig) {
                        if (configEntry["name"] == varName && configEntry["line"] == std::to_string(lineNo)) {
                            Type *targetType = nullptr;
                            auto typeName = configEntry["type"].get<string>();

                            // If new type is same as before, skip
                            if (typeName == parseDITypeName(DV->getType())) continue;
                            if (typeName == "float") {
                                targetType = Type::getFloatTy(context);
                            } else if (typeName == "double") {
                                targetType = Type::getDoubleTy(context);
                            } else if (typeName == "long double") {
                                targetType = Type::getFP128Ty(context);
                            } else if (typeName == "int") {
                                targetType = Type::getInt32Ty(context);
                            } else if (typeName == "long") {
                                targetType = Type::getInt64Ty(context);
                            } else if (typeName == "long long") {
                                targetType = Type::getInt128Ty(context);
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
    // Check if call inst need to be changed
    // TODO: 只实现了将float->double的情况
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                Function *callee = call->getCalledFunction();
                if (callee && callee->getName() == "llvm.fmuladd.f32") {
                    for (unsigned i = 0; i < 3; ++i) {  // 有4个操作数，但第四个固定为ptr
                        Value *arg = call->getOperand(i);
                        if (!arg->getType()->isFloatTy()) {
                            modified |= changeCallPrecision(call, "llvm.fmuladd.f64");
                            break;  // 同一条只需要替换一次
                        }   
                    }
                }
            }
        }
    }
    return modified;
}

bool ChangePrecision::runOnModule(Module &module) {
    parseIncludedFunctions();
    parseTypeConfig();
    bool modified = false;
    for(Module::iterator f = module.begin(), fe = module.end(); f != fe; f++) {
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