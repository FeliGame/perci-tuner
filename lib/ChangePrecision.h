// @Author: Guocheng - NUDT

#ifndef LLVM_CHANGE_PRECISION_H
#define LLVM_CHANGE_PRECISION_H

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

#include "nlohmann/json.hpp"

namespace llvm {


class ChangePrecision : public PassInfoMixin<ChangePrecision> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void parseIncludedFunctions();

    void parseTypeConfig();

    bool changeAllocaInst(DbgDeclareInst *DDI, Type *newType);

    bool repairFMulAdd(CallInst *call, Type* argType);

    Value *insertCast(Value *val, Type *targetType, Instruction *insertBefore);

    bool repairUserType(User *user);

    bool repairCasts(Function &F);

    Type* translateTypeName(std::string typeName, LLVMContext& context);

    std::set<std::string> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    DenseSet<Instruction*> instRepaired;   // Inst repaired by repairUserType()

    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_CHANGE_PRECISION_H