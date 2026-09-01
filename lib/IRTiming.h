// @Author: Guocheng - NUDT

#ifndef LLVM_IR_TIMING_H
#define LLVM_IR_TIMING_H

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>

#include <vector>

using namespace std;
using namespace llvm;

class IRTiming : public PassInfoMixin<IRTiming> {
public:
    		
    PreservedAnalyses run(Module &module, ModuleAnalysisManager &); // for New Pass Manager

    virtual bool runOnModule(Module &M);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
};


#endif // LLVM_IR_TIMING_H