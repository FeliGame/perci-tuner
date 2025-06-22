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
    
    void runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void parseConfig();

    AllocaInst* changeAllocaInst(DbgDeclareInst *DDI, Type *newType);

    Value* insertCast(Value *val, Type *targetType, Instruction *insertBefore);

    Type* findHighestPrecisionType(BinaryOperator*);
    
    Type* findHighestPrecisionType(FCmpInst*);

    Type* findHighestPrecisionType(PHINode*);

    Instruction* repairReturnInst(ReturnInst *ret);

    Instruction* repairLoadInst(LoadInst *load);

    Instruction* repairStoreInst(StoreInst *store);

    Instruction* repairUnaryOp(UnaryOperator *unaOp);

    Instruction* repairBinaryOp(BinaryOperator *binOp);

    Instruction* repairFCmpInst(FCmpInst *fcmp);

    Instruction* repairCallInst(CallInst *call);

    Instruction* repairPHINode(PHINode *phi);

    void repairAllocaUsers(AllocaInst* alloca);

    void repairUserType(User *user);

    void repairCasts(Function &F);

    AllocaInst* findOriginalAlloca(Value* value);

    Type* translateValidTypeName(std::string typeName, LLVMContext& context);

    void changeStructDeclarations(Module &module);

    void updateMultiOpReplacement(Instruction* oldInst, Instruction* newInst);

    void removeRedundantReplacements();

    std::string getDDBinOpCallName(BinaryOperator *bo);
    template <typename T>
    Instruction* changeToDDCall(T *I, const std::string &FunctionName, const std::vector<Value *> &Args, Type *ReturnType);
    void handleFP128ToDD(Instruction &I);

    DenseSet<StringRef> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    DenseSet<AllocaInst*> newAllocas;

    DenseMap<Instruction*, Instruction*> instReplaceMap;    // MultiOperandInst replacement

    DenseMap<AllocaInst*, Type*> pointeeTypeMap;  // AllocaInst's new pointee type
    
    nlohmann::json config;
};

} // namespace llvm

#endif // LLVM_CHANGE_VARIABLE_DECL_H