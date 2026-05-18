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

// 函数调用图节点
class CallGraphNode {
public:
    Function* func; // 当前函数体
    DenseMap<CallInst*, CallGraphNode*> children;  // 后继调用
    CallGraphNode(Function* f) : func(f) {}
};

class ChangeVariableDecl : public PassInfoMixin<ChangeVariableDecl> {
public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &module);
    
    void runOnFunction(Function &F);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void parseConfig();

    Value* insertCast(Value *val, Type *targetType, Instruction *insertBefore);

    Type* findHighestPrecisionType(BinaryOperator*);
    Type* findHighestPrecisionType(FCmpInst*);
    Type* findHighestPrecisionType(PHINode*);

    Align getAlign(Type* targetType);
    DIType* createDIType(Type* type, DIScope* scope, DIBuilder* DIB);
    
    Type* typeNameToType(std::string typeName, LLVMContext& context);
    AllocaInst* findOriginalAlloca(Value* value);

    Type* getBasicType(Value *value);
    DenseMap<GlobalVariable*, Type*> globalBaseTypeMap;  // Global variable to its base type
    DenseMap<AllocaInst*, Type*> localBaseTypeMap;  // Local variable to its base type

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
    void repairGVUsers(GlobalVariable *GV, Type* targetType);
    void repairCasts(Function &F);

    AllocaInst* changeAllocaInst(AllocaInst *alloca, Type *newType);
    void changeStructDeclarations(Module &module);
    Constant* changeGVInitializer(Constant *initializer, Type* targetType);
    void changeGlobalVariable(GlobalVariable *GV, Type* targetType);
    void changeGlobalDeclarations(Module &module);

    void replaceMultiOperandInst(Instruction* oldInst, Instruction* newInst);

    void removeUnusedReplacements();

    DenseSet<StringRef> includedFunctions;

    DenseSet<Instruction*> instToErase;  // Inst to be erased later

    DenseSet<GlobalVariable*> GVToErase;

    DenseSet<AllocaInst*> newAllocas;

    DenseMap<Instruction*, Instruction*> instReplaceMap;    // MultiOperandInst replacement

    DenseMap<AllocaInst*, Type*> pointeeTypeMap;  // AllocaInst's new pointee type

    DenseMap<Function*, CallGraphNode*> funcToNode;
    std::map<std::string, Function*> cloneCache;
    DenseMap<Argument*, Type*> argPtrBaseType;  // 函数的浮点指针形参基类型
    CallGraphNode* buildCallGraph(Function* function);
    void updateCallGraph(CallGraphNode* root);
    void dumpCallGraph(CallGraphNode* root, int layer, DenseSet<Function*> &visited);
    void updateCallInst(CallInst *call, Function &newFunc, std::map<int, Type*> &declTypeMap);
    Function* cloneFunction(Function *oldFunc, std::map<int, Type*> &argBaseTypes);
    DenseMap<Function*, std::map<int, Type*>> funcDeclTypeMap;  // 函数浮点参数类型
    DenseMap<Function*, Type*> funcRetTypeMap;  // 函数返回类型
    
    DenseMap<CallInst*, std::vector<Type*>> varargCallOriginalTypes;  // printf等可变参数
    
    nlohmann::json config;

    std::map<std::string, nlohmann::json> configMap;  // 存储config的map形式，key为file:line:column的拼接字符串

    DIBuilder *DIB;

    const DataLayout *DL;
};

} // namespace llvm

#endif // LLVM_CHANGE_VARIABLE_DECL_H