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

    AllocaInst* changeAllocaInst(AllocaInst *alloca, Type *newType);

    Value* insertCast(Value *val, Type *targetType, Instruction *insertBefore);

    Type* findHighestPrecisionType(BinaryOperator*);
    
    Type* findHighestPrecisionType(FCmpInst*);

    Type* findHighestPrecisionType(PHINode*);

    Align getAlign(Type* targetType);

    Instruction* repairReturnInst(ReturnInst *ret);

    Instruction* repairLoadInst(LoadInst *load, Type* targetType=nullptr, unsigned align=0);

    Instruction* repairStoreInst(StoreInst *store, Type* targetType=nullptr, unsigned align=0);

    Instruction* repairUnaryOp(UnaryOperator *unaOp);

    Instruction* repairBinaryOp(BinaryOperator *binOp);

    Instruction* repairFCmpInst(FCmpInst *fcmp);

    Instruction* repairCallInst(CallInst *call);

    Instruction* repairPHINode(PHINode *phi);

    Instruction* repairGEPInst(GetElementPtrInst* gep, Type* targetType);

    Instruction* repairBitCastInst(BitCastInst* bitCast);

    Instruction* repairIntToFP(Instruction* castInst);

    void repairAllocaUsers(AllocaInst* alloca);

    void repairUserType(User *user, Type* targetType=nullptr, unsigned align=0);

    void repairCasts(Function &F);

    AllocaInst* findOriginalAlloca(Value* value);

    Type* translateValidTypeName(std::string typeName, LLVMContext& context);

    void changeStructDeclarations(Module &module);
    
    Function* changeFunctionDecl(Function &F, DenseMap<int, Type*> declTypeMap);

    void updateCaller(Function &oldFunc, Function &newFunc, DenseMap<int, Type*>& declTypeMap);

    void replaceMultiOperandInst(Instruction* oldInst, Instruction* newInst);

    void removeUnusedReplacements();

    DIType* createDIType(Type* type, DIScope* scope, DIBuilder* DIB);

    DenseSet<StringRef> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    DenseSet<Function*> funcToErase;    // Function to be erased later

    DenseSet<AllocaInst*> newAllocas;

    DenseMap<Instruction*, Instruction*> instReplaceMap;    // MultiOperandInst replacement

    DenseMap<AllocaInst*, Type*> pointeeTypeMap;  // AllocaInst's new pointee type

    DenseMap<Function*, DenseMap<int, Type*>> funcDeclTypeMap;  // 函数声明类型原型，-1表示返回值，0...表示参数索引
    
    DenseMap<CallInst*, std::vector<Type*>> varargCallOriginalTypes;  // printf等可变参数
    
    nlohmann::json config;

    std::map<std::string, nlohmann::json> configMap;  // 存储config的map形式，key为file:line:column的拼接字符串

    DIBuilder *DIB;

    const DataLayout *DL;
};

} // namespace llvm

#endif // LLVM_CHANGE_VARIABLE_DECL_H