// @Author: Guocheng - NUDT
// Usage: Change CallInst according to json.
// Input: Function's type config
// Output: CallInst-changed IR

#include "ChangeFunctionCall.h"
#include "utils/ParseTypeName.hpp"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Demangle/Demangle.h>

#include <fstream>

using namespace std;
using namespace llvm;


cl::opt<string> FunctionConfigName("function-config", cl::value_desc("filename"), cl::desc("Function's Type Config"), cl::init("config.json"));

bool ChangeFunctionCall::runOnFunction(Function &F) {
    auto getSrcOperand = [](Value* operand) -> Value* {
        // 处理CastInst：用转换指令的源操作数替换所有使用
        auto* srcOp = operand;
        if (auto* castInst = dyn_cast<CastInst>(operand)) {
            srcOp = castInst->getOperand(0);
            operand->replaceAllUsesWith(srcOp);
            castInst->eraseFromParent();
        }
        return srcOp;
    };

    bool modified = false;
    auto &Context = F.getContext();
    auto funcName = demangle(F.getName());
    nlohmann::json funcInfo = config[funcName];

    for (auto &BB : F) {
        SmallVector<CallInst*, 4> toDelete;  // Delete later to avoid break iterator
        for (auto &I : BB) {
            if (auto *call = dyn_cast<CallInst>(&I)) {
                auto *oldFunc = call->getCalledFunction();
                if (!oldFunc) continue;
                StringRef oldCallee = oldFunc->getName();
                
                if (!mathFunctions.contains(oldCallee)) continue;

                auto loc = call->getDebugLoc();
                if (!loc) continue;

                for (auto &entry : funcInfo) {
                    if (entry["file"] == loc->getFilename().str() &&
                        entry["line"] == std::to_string(loc.getLine()) &&
                        entry["column"] == std::to_string(loc.getCol())) {
                        string targetCallee = entry["type"].get<string>();
                        // If target is same as original or non-math, skip
                        if (targetCallee == oldCallee || !mathFunctions.contains(targetCallee)) continue;

                        auto getBaseName = [](StringRef name) {
                            if (name.size() > 0 && (name.back() == 'f' || name.back() == 'l')) {
                                return name.drop_back(); // 去除精度后缀
                            }
                            return name;
                        };
                    
                        StringRef oldBase = getBaseName(oldCallee);
                        StringRef newBase = getBaseName(targetCallee);
                    
                        if (oldBase != newBase) {
                            errs() << "Ignored: Base name mismatch [" 
                                   << oldCallee << " vs " << targetCallee << "]\n";
                            continue;
                        }                    
                        errs() << "current: " << oldCallee << ", changed to: " << targetCallee << "\n";

                        Type *paramType;
                        if (targetCallee.back() == 'f') {
                            paramType = Type::getFloatTy(Context);
                        } else if (targetCallee.back() == 'l') {
                            paramType = Type::getFP128Ty(Context); 
                        } else {
                            paramType = Type::getDoubleTy(Context);
                        }
                        // All args are of same type
                        SmallVector<Type*, 2> paramTypes;
                        for (auto &op : call->args()) {
                            paramTypes.push_back(paramType);
                        }
                        
                        auto *newFuncType = FunctionType::get(
                            paramType,  // ret type
                            paramTypes, // paras' type
                            false
                        );
                        
                        // Insert cast for paras if needed
                        IRBuilder<> Builder(call);
                        SmallVector<Value*, 2> castedArgs;
                        
                        for (auto &arg : call->args()) {
                            auto* srcOp = getSrcOperand(arg);
                            auto* srcTy = srcOp->getType();
                            if (srcTy == paramType) 
                                castedArgs.push_back(srcOp);
                            else if (srcTy->isIntegerTy())
                                castedArgs.push_back(
                                    Builder.CreateSIToFP(srcOp, paramType, "arg_cast")
                                );
                            else
                                castedArgs.push_back(
                                    Builder.CreateFPCast(srcOp, paramType, "arg_cast")
                                );
                        }
                        
                        auto newFunc = F.getParent()->getOrInsertFunction(
                            targetCallee, 
                            newFuncType
                        );
                        
                        auto *newCall = Builder.CreateCall(
                            newFunc, 
                            castedArgs
                        );
                        
                        // ret type cast
                        Value *finalResult = newCall;
                        if (call->getType() != newCall->getType()) {
                            finalResult = Builder.CreateFPCast(
                                newCall, 
                                call->getType(),
                                "ret_cast"
                            );
                        }

                        call->replaceAllUsesWith(finalResult);
                        toDelete.push_back(call);
                        modified = true;
                    }
                }
            }
        }

        for (auto *call : toDelete) {
            call->eraseFromParent();
        }
    }

    return modified;
}

bool ChangeFunctionCall::runOnModule(Module &module) {
    ifstream inFile(FunctionConfigName.c_str());
    if (!inFile) {
        errs() << "Error: Failed to open " << FunctionConfigName << "\n";
        exit(1);
    }
    inFile >> config;
    config = config["calls"];
    
    bool modified = false;
    for(auto &F: module) {
        if(!F.isDeclaration()) {
            modified |= runOnFunction(F);
        }
    }

    return modified;
}  

void ChangeFunctionCall::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesAll(); 
}

PreservedAnalyses ChangeFunctionCall::run(Module &M, ModuleAnalysisManager &) {
    errs() << ">>> Perci-Tuner: Running ChangeFunctionCall Pass...\n";
    if (!runOnModule(M))
        return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}
