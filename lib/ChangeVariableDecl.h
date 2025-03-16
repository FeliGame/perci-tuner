// @Author: Guocheng - NUDT

#ifndef LLVM_CHANGE_VARIABLE_DECL_H
#define LLVM_CHANGE_VARIABLE_DECL_H

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>

#include "nlohmann/json.hpp"

namespace llvm {


class ChangeVariableDecl : public PassInfoMixin<ChangeVariableDecl> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    bool runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void parseConfig();

    bool changeAllocaInst(DbgDeclareInst *DDI, Type *newType);

    Value *insertCast(Value *val, Type *targetType, Instruction *insertBefore);

    Type* findHighestPrecisionType(BinaryOperator*);
    
    Type* findHighestPrecisionType(FCmpInst*);

    Type* findHighestPrecisionType(CallInst*);

    Type* findHighestPrecisionType(PHINode*);

    bool repairUserType(User *user);

    bool repairCasts(Function &F);

    AllocaInst* findOriginalAlloca(Value* value);

    void repairLoadUsers(LoadInst* newLoad, Type* newType);

    Type* translateValidTypeName(std::string typeName, LLVMContext& context);

    bool changeStructDeclarations(Module &module);

    DenseSet<StringRef> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_CHANGE_VARIABLE_DECL_H