// @Author: Guocheng - NUDT

#ifndef LLVM_CHANGE_VARIABLE_DECL_H
#define LLVM_CHANGE_VARIABLE_DECL_H

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/DIBuilder.h>

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
    
    Function* changeFunctionArgTypes(Function &F, DenseMap<unsigned, Type*> argNewTypeMap);

    void updateCallersArgTypes(Function &oldFunc, Function &newFunc, DenseMap<unsigned, Type*>& argNewTypeMap);

    void replaceMultiOperandInst(Instruction* oldInst, Instruction* newInst);

    void removeUnusedReplacements();

    DIType* createDIType(Type* type, DIScope* scope, DIBuilder* DIB);

    DenseSet<StringRef> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    DenseSet<AllocaInst*> newAllocas;

    DenseMap<Instruction*, Instruction*> instReplaceMap;    // MultiOperandInst replacement

    DenseMap<AllocaInst*, Type*> pointeeTypeMap;  // AllocaInst's new pointee type

    DenseMap<Function*, DenseMap<unsigned, Type*>> funcArgNewTypeMap;

    DenseMap<CallInst*, std::vector<Type*>> varargCallOriginalTypes;  // printf等可变参数
    
    nlohmann::json config;

    DIBuilder *DIB;
};

} // namespace llvm

#endif // LLVM_CHANGE_VARIABLE_DECL_H