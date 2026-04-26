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

    Constant* changeGVInitializer(Constant *initializer, Type* targetType);

    void repairGVUsers(GlobalVariable *GV, Type* targetType);

    void repairCasts(Function &F);

    AllocaInst* findOriginalAlloca(Value* value);

    Type* typeNameToType(std::string typeName, LLVMContext& context);

    void changeStructDeclarations(Module &module);

    void changeGlobalVariable(GlobalVariable *GV, Type* targetType);

    void changeGlobalDeclarations(Module &module);

    void updateCaller(CallInst *call, Function &newFunc, std::map<int, Type*> &declTypeMap);

    void replaceMultiOperandInst(Instruction* oldInst, Instruction* newInst);

    void removeUnusedReplacements();

    DIType* createDIType(Type* type, DIScope* scope, DIBuilder* DIB);

    void updateCalleeToCallInsts(Function *F);

    Function* cloneFunctionWithNewArgs(Function *oldFunc, std::map<int, Type*> &argBaseTypes);

    Type* getBasicType(Value *value);

    DenseSet<StringRef> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    DenseSet<GlobalVariable*> GVToErase;

    DenseSet<AllocaInst*> newAllocas;

    DenseMap<Instruction*, Instruction*> instReplaceMap;    // MultiOperandInst replacement
    
    DenseMap<CallInst*, Function*> functionReplaceMap;    // Multi-Version Function replacement

    DenseMap<AllocaInst*, Type*> pointeeTypeMap;  // AllocaInst's new pointee type

    // <被调函数，<参数索引，实参类型>> -> 该被调函数版本的CallInst列表
    std::map<std::pair<Function*, std::map<int, Type*>>, SmallVector<CallInst*>> calleeToCallInsts;

    DenseMap<Function*, Type*> funcRetTypeMap;  // 函数返回类型

    DenseSet<Function*> funcToErase;  // 被替换的旧函数副本
    
    DenseMap<CallInst*, std::vector<Type*>> varargCallOriginalTypes;  // printf等可变参数
    
    DenseMap<GlobalVariable*, Type*> globalBaseTypeMap;  // Global variable to its base type
    DenseMap<AllocaInst*, Type*> localBaseTypeMap;  // Local variable to its base type
    
    nlohmann::json config;

    std::map<std::string, nlohmann::json> configMap;  // 存储config的map形式，key为file:line:column的拼接字符串

    DIBuilder *DIB;

    const DataLayout *DL;
};

} // namespace llvm

#endif // LLVM_CHANGE_VARIABLE_DECL_H