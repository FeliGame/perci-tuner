// @Author: Guocheng - NUDT

#ifndef LLVM_RECORD_RESULT_H
#define LLVM_RECORD_RESULT_H

#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Passes/PassBuilder.h>

namespace llvm {

class RecordResult : public PassInfoMixin<RecordResult> {
    public:
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
    void searchResultVariable(Module &M);

    void createGlobalRecord(Module &M);

    void insertPrintFP(Module &M);

    void insertCheckRE(Module &M);

    Type* convertDITypeToLLVMType(DIType* diType, LLVMContext& context);
    
    DILocalVariable* resultVar;

    Type* resultVarType;

    GlobalVariable* globalRecordVar;

    std::string resultVarName;

    DenseSet<Instruction*> instToErase;
};

}  // namespace llvm
#endif // LLVM_RECORD_RESULT_H
